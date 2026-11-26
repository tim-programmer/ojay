// The compiler driver: Jai source in, one object file (or assembly / LLVM IR)
// out. Linking is deliberately not done here; see README.md for how to link
// objects against the runtime for each target.
#include "driver.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Triple.h>

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "codegen.h"
#include "parser.h"
#include "sema.h"

#ifndef OJAY_VERSION
#define OJAY_VERSION "0.1.0"
#endif

namespace ojay {
namespace {

enum class Emit { Object, Assembly, LLVMIR };

struct Options {
  std::string input;
  std::string output;
  Emit emit = Emit::Object;
  int opt_level = 0;
  std::string triple, cpu, features;
  bool pic = false;
  std::vector<std::string> import_dirs;
  bool bounds_checks = true;
};

void usage(const TargetDesc& t, std::ostream& os) {
  os << "usage: " << t.tool_name << " [options] <file.jai>\n"
     << "\n"
     << "Compiles a Jai program to a " << t.arch << " object file (default triple " << t.triple << ").\n"
     << "Link the object against the ojay runtime with your linker; see README.md.\n"
     << "\n"
     << "options:\n"
     << "  -o <path>           output file (default: input name with .o / .s / .ll)\n"
     << "  -c                  emit an object file (the default)\n"
     << "  -S                  emit target assembly\n"
     << "  --emit-llvm         emit LLVM IR (.ll)\n"
     << "  -O0 | -O1 | -O2 | -O3   optimization level (default -O0)\n"
     << "  --target <triple>   override the target triple\n"
     << "  --cpu <name>        override the target CPU (default " << t.cpu << ")\n"
     << "  --features <list>   override target features\n"
     << "  --pic / --no-pic    position-independent code (default " << (t.pic_by_default ? "--pic" : "--no-pic")
     << ")\n"
     << "  -I <dir>            add a module search directory\n"
     << "  --no-bounds-check   disable array bounds checking\n"
     << "  --version           print version information\n"
     << "  -h, --help          show this help\n";
}

bool file_exists(const std::string& p) { return llvm::sys::fs::exists(p) && !llvm::sys::fs::is_directory(p); }

bool read_file(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

void print_diagnostic(const SourceManager& sm, SourceLoc loc, const char* kind, const std::string& msg) {
  if (loc.line == 0) {
    std::cerr << kind << ": " << msg << "\n";
    return;
  }
  std::cerr << sm.describe(loc) << ": " << kind << ": " << msg << "\n";
  std::string line = sm.line_text(loc);
  if (!line.empty()) {
    std::cerr << "    " << line << "\n    ";
    for (uint32_t i = 1; i < loc.col && i - 1 < line.size(); ++i) std::cerr << (line[i - 1] == '\t' ? '\t' : ' ');
    std::cerr << "^\n";
  }
}

void print_error(const SourceManager& sm, const CompileError& e) { print_diagnostic(sm, e.loc, "error", e.what()); }

void print_warnings(const SourceManager& sm, const Program& prog) {
  for (auto& w : prog.warnings) print_diagnostic(sm, w.loc, "warning", w.message);
}

// Loads the main file and everything it #loads / #imports. Each imported
// module is a separate unit (with its own scope in sema); the files a unit
// #loads belong to it.
class Loader {
 public:
  Loader(AstContext& ast, SourceManager& sm, Program& prog, const std::vector<std::string>& import_dirs)
      : ast_(ast), sm_(sm), prog_(prog), import_dirs_(import_dirs) {}

  void load_main(const std::string& path) {
    prog_.modules.push_back({"", ModuleUnit::Kind::Main, {}});
    load_file(path, SourceLoc{}, 0);
  }

 private:
  AstContext& ast_;
  SourceManager& sm_;
  Program& prog_;
  const std::vector<std::string>& import_dirs_;
  std::set<std::string> loaded_;
  std::map<std::string, int> modules_;  // module name -> unit index

  void load_file(const std::string& path, SourceLoc from, int unit) {
    llvm::SmallString<256> abs(path);
    llvm::sys::fs::make_absolute(abs);
    llvm::sys::path::remove_dots(abs, true);
    std::string key(abs);
    if (!loaded_.insert(key).second) return;
    std::string text;
    if (!read_file(path, text)) fail(from, "cannot open file '", path, "'");
    uint32_t id = sm_.add(path, std::move(text));
    std::vector<Stmt*> stmts = parse_file(ast_, sm_, id);
    std::string dir(llvm::sys::path::parent_path(path));
    scan(stmts, dir, unit);
    auto& tl = prog_.modules[unit].top_level;
    tl.insert(tl.end(), stmts.begin(), stmts.end());
  }

  void scan(const std::vector<Stmt*>& stmts, const std::string& dir, int unit) {
    for (Stmt* s : stmts) {
      if (s->kind == NK::Load) {
        auto* l = static_cast<Load*>(s);
        std::string p = l->path;
        if (!llvm::sys::path::is_absolute(p) && !dir.empty()) p = dir + "/" + p;
        load_file(p, s->loc, unit);
      } else if (s->kind == NK::Import) {
        auto* im = static_cast<Import*>(s);
        im->module_id = import_module(im->module, s->loc, dir);
      } else if (s->kind == NK::Decl) {
        // `Name :: #import "Module";`
        auto* d = static_cast<Decl*>(s);
        if (d->is_const && d->values.size() == 1 && d->values[0]->kind == NK::Directive) {
          auto* de = static_cast<DirectiveExpr*>(d->values[0]);
          if (de->name.rfind("import:", 0) == 0) de->module_id = import_module(de->name.substr(7), de->loc, dir);
        }
      } else if (s->kind == NK::StaticIf) {
        // Imports/loads inside #if blocks are loaded unconditionally; sema only
        // makes the taken branch's imports visible.
        auto* si = static_cast<StaticIf*>(s);
        scan(si->then_s, dir, unit);
        scan(si->else_s, dir, unit);
      }
    }
  }

  int import_module(const std::string& name, SourceLoc loc, const std::string& dir) {
    auto it = modules_.find(name);
    if (it != modules_.end()) return it->second;
    int unit = (int)prog_.modules.size();
    std::vector<std::string> dirs = import_dirs_;
    dirs.insert(dirs.begin(), dir.empty() ? "." : dir + "/modules");
    for (const std::string& d : dirs) {
      std::string a = d + "/" + name + ".jai";
      std::string b = d + "/" + name + "/module.jai";
      std::string found = file_exists(a) ? a : file_exists(b) ? b : "";
      if (found.empty()) continue;
      prog_.modules.push_back({name, ModuleUnit::Kind::File, {}});
      modules_[name] = unit;
      load_file(found, loc, unit);
      return unit;
    }
    // Modules implemented inside the compiler.
    if (name == "Basic" || name == "Math" || name == "Hardware") {  // Hardware: ojay extension
      if (name == "Basic") prog_.import_basic = true;
      if (name == "Math") prog_.import_math = true;
      if (name == "Hardware") prog_.import_hardware = true;
      prog_.modules.push_back({name, ModuleUnit::Kind::Builtin, {}});
      modules_[name] = unit;
      return unit;
    }
    if (name == "String" || name == "Compiler" || name == "POSIX" || name == "Random" || name == "File" ||
        name == "Hash_Table" || name == "Thread") {
      fail(loc, "module \"", name, "\" is not available yet (built-in modules: Basic, Math, Hardware)");
    }
    fail(loc, "module \"", name, "\" not found (searched ", dirs.size(), " director",
         dirs.size() == 1 ? "y" : "ies", "; use -I to add more)");
  }
};

std::string default_output(const Options& o) {
  std::string stem(llvm::sys::path::stem(o.input));
  switch (o.emit) {
    case Emit::Object: return stem + ".o";
    case Emit::Assembly: return stem + ".s";
    case Emit::LLVMIR: return stem + ".ll";
  }
  return stem;
}

}  // namespace

int driver_main(int argc, char** argv, const TargetDesc& target) {
  Options o;
  o.triple = target.triple;
  o.cpu = target.cpu;
  o.features = target.features;
  o.pic = target.pic_by_default;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto need = [&](const char* what) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << target.tool_name << ": missing argument for " << what << "\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "-h" || a == "--help") {
      usage(target, std::cout);
      return 0;
    } else if (a == "--version") {
      std::cout << target.tool_name << " " << OJAY_VERSION << " (Jai frontend, LLVM backend; target " << target.triple
                << ")\n";
      return 0;
    } else if (a == "-o") {
      o.output = need("-o");
    } else if (a == "-c") {
      o.emit = Emit::Object;
    } else if (a == "-S") {
      o.emit = Emit::Assembly;
    } else if (a == "--emit-llvm") {
      o.emit = Emit::LLVMIR;
    } else if (a.size() == 3 && a[0] == '-' && a[1] == 'O' && a[2] >= '0' && a[2] <= '3') {
      o.opt_level = a[2] - '0';
    } else if (a == "--target") {
      o.triple = need("--target");
    } else if (a == "--cpu") {
      o.cpu = need("--cpu");
    } else if (a == "--features") {
      o.features = need("--features");
    } else if (a == "--pic") {
      o.pic = true;
    } else if (a == "--no-pic") {
      o.pic = false;
    } else if (a == "-I") {
      o.import_dirs.push_back(need("-I"));
    } else if (a.rfind("-I", 0) == 0 && a.size() > 2) {
      o.import_dirs.push_back(a.substr(2));
    } else if (a == "--no-bounds-check") {
      o.bounds_checks = false;
    } else if (!a.empty() && a[0] == '-') {
      std::cerr << target.tool_name << ": unknown option '" << a << "' (see --help)\n";
      return 2;
    } else if (o.input.empty()) {
      o.input = a;
    } else {
      std::cerr << target.tool_name << ": only one input file is supported (got '" << o.input << "' and '" << a
                << "')\n";
      return 2;
    }
  }
  if (o.input.empty()) {
    usage(target, std::cerr);
    return 2;
  }
  if (o.output.empty()) o.output = default_output(o);

  // ---------------------------------------------------------------- frontend
  SourceManager sm;
  AstContext ast;
  Program prog;
  TypeTable types(TargetLayout{target.pointer_size});
  try {
    Loader loader(ast, sm, prog, o.import_dirs);
    loader.load_main(o.input);
    analyze(ast, sm, types, prog, SemaOptions{});
  } catch (const CompileError& e) {
    print_warnings(sm, prog);
    print_error(sm, e);
    return 1;
  }
  print_warnings(sm, prog);

  // ---------------------------------------------------------------- backend
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();

  o.triple = llvm::Triple::normalize(o.triple);
  llvm::Triple triple(o.triple);
  std::string err;
  const llvm::Target* tgt = llvm::TargetRegistry::lookupTarget(triple, err);
  if (!tgt) {
    std::cerr << target.tool_name << ": unknown target '" << o.triple << "': " << err << "\n";
    return 1;
  }
  llvm::TargetOptions topts;
  if (triple.getEnvironment() == llvm::Triple::EABIHF || triple.getEnvironment() == llvm::Triple::GNUEABIHF)
    topts.FloatABIType = llvm::FloatABI::Hard;
  llvm::Reloc::Model reloc = o.pic ? llvm::Reloc::PIC_ : llvm::Reloc::Static;
  llvm::CodeGenOptLevel cg_level = o.opt_level == 0   ? llvm::CodeGenOptLevel::None
                                   : o.opt_level == 1 ? llvm::CodeGenOptLevel::Less
                                   : o.opt_level == 2 ? llvm::CodeGenOptLevel::Default
                                                      : llvm::CodeGenOptLevel::Aggressive;
  std::unique_ptr<llvm::TargetMachine> tm(
      tgt->createTargetMachine(triple, o.cpu, o.features, topts, reloc, std::nullopt, cg_level));
  if (!tm) {
    std::cerr << target.tool_name << ": could not create target machine for '" << o.triple << "'\n";
    return 1;
  }

  llvm::LLVMContext ctx;
  std::unique_ptr<llvm::Module> mod;
  try {
    CodegenOptions copts;
    copts.bounds_checks = o.bounds_checks;
    copts.module_name = o.input;
    // Match the AAPCS bare-metal ABI used by arm-none-eabi libraries.
    copts.short_enums = target.platform == Platform::BareMetal && (triple.isARM() || triple.isThumb());
    mod = generate_module(ctx, tm->createDataLayout(), o.triple, prog, types, sm, copts);
  } catch (const CompileError& e) {
    print_error(sm, e);
    return 1;
  }

  // Optimization pipeline.
  {
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;
    llvm::PassBuilder pb(tm.get());
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);
    llvm::OptimizationLevel level = o.opt_level == 0   ? llvm::OptimizationLevel::O0
                                    : o.opt_level == 1 ? llvm::OptimizationLevel::O1
                                    : o.opt_level == 2 ? llvm::OptimizationLevel::O2
                                                       : llvm::OptimizationLevel::O3;
    llvm::ModulePassManager mpm = o.opt_level == 0 ? pb.buildO0DefaultPipeline(level)
                                                   : pb.buildPerModuleDefaultPipeline(level);
    mpm.run(*mod, mam);
  }

  std::error_code ec;
  llvm::raw_fd_ostream os(o.output, ec, o.emit == Emit::Object ? llvm::sys::fs::OF_None : llvm::sys::fs::OF_Text);
  if (ec) {
    std::cerr << target.tool_name << ": cannot write '" << o.output << "': " << ec.message() << "\n";
    return 1;
  }
  if (o.emit == Emit::LLVMIR) {
    mod->print(os, nullptr);
    return 0;
  }
  llvm::legacy::PassManager pm;
  auto kind = o.emit == Emit::Assembly ? llvm::CodeGenFileType::AssemblyFile : llvm::CodeGenFileType::ObjectFile;
  if (tm->addPassesToEmitFile(pm, os, nullptr, kind)) {
    std::cerr << target.tool_name << ": the target cannot emit this file type\n";
    return 1;
  }
  pm.run(*mod);
  return 0;
}

}  // namespace ojay
