#include "types.h"

#include <algorithm>

namespace ojay {

static int64_t align_up(int64_t v, int64_t a) { return (v + a - 1) / a * a; }

TypeTable::TypeTable(TargetLayout layout) : layout_(layout) {
  const int ps = layout_.pointer_size;
  void_t = make(TypeKind::Void, "void", 0, 1);
  bool_t = make(TypeKind::Bool, "bool", 1, 1);
  auto mk_int = [&](const char* n, int bits, bool sgn) {
    Type* t = make(TypeKind::Int, n, bits / 8, bits / 8);
    t->bits = bits;
    t->is_signed = sgn;
    return t;
  };
  s8 = mk_int("s8", 8, true);
  s16 = mk_int("s16", 16, true);
  s32 = mk_int("s32", 32, true);
  s64 = mk_int("s64", 64, true);
  u8 = mk_int("u8", 8, false);
  u16 = mk_int("u16", 16, false);
  u32 = mk_int("u32", 32, false);
  u64 = mk_int("u64", 64, false);
  f32 = make(TypeKind::Float, "float32", 4, 4);
  f32->bits = 32;
  f64 = make(TypeKind::Float, "float64", 8, 8);
  f64->bits = 64;
  // string is { count: s64; data: *u8; }
  string_t = make(TypeKind::String, "string", align_up(8 + ps, 8), 8);
  // Any is { type: *Type_Info; value_pointer: *void; }
  any_t = make(TypeKind::Any, "Any", 2 * ps, ps);
  type_t = make(TypeKind::TypeT, "Type", ps, ps);
  // A Code value is an id into the compiler's table of #code syntax trees.
  code_t = make(TypeKind::Code, "Code", 8, 8);
  untyped_int = make(TypeKind::UntypedInt, "untyped integer", 8, 8);
  untyped_float = make(TypeKind::UntypedFloat, "untyped float", 8, 8);
  null_t = make(TypeKind::Null, "null", ps, ps);
  void_ptr = pointer_to(void_t);
}

Type* TypeTable::make(TypeKind k, const std::string& name, int64_t size, int64_t align) {
  owned_.push_back(std::make_unique<Type>(k));
  Type* t = owned_.back().get();
  t->name = name;
  t->size = size;
  t->align = align;
  return t;
}

Type* TypeTable::int_type(int bits, bool is_signed) {
  switch (bits) {
    case 8: return is_signed ? s8 : u8;
    case 16: return is_signed ? s16 : u16;
    case 32: return is_signed ? s32 : u32;
    default: return is_signed ? s64 : u64;
  }
}

Type* TypeTable::pointer_to(Type* elem) {
  auto it = pointers_.find(elem);
  if (it != pointers_.end()) return it->second;
  const int ps = layout_.pointer_size;
  Type* t = make(TypeKind::Pointer, "", ps, ps);
  t->elem = elem;
  pointers_[elem] = t;
  return t;
}

Type* TypeTable::array_of(Type* elem, ArrayKind kind, int64_t count) {
  if (kind != ArrayKind::Fixed) count = 0;
  auto key = std::make_tuple(elem, (int)kind, count);
  auto it = arrays_.find(key);
  if (it != arrays_.end()) return it->second;
  const int ps = layout_.pointer_size;
  Type* t = nullptr;
  switch (kind) {
    case ArrayKind::Fixed:
      // Element size is known by the time a fixed array is formed (the
      // checker completes struct layout first).
      t = make(TypeKind::Array, "", elem->size * count, elem->align);
      break;
    case ArrayKind::View:  // { count: s64; data: *T; }
      t = make(TypeKind::Array, "", align_up(8 + ps, 8), 8);
      break;
    case ArrayKind::Dynamic:  // { count: s64; data: *T; allocated: s64; }
      t = make(TypeKind::Array, "", align_up(align_up(8 + ps, 8) + 8, 8), 8);
      break;
  }
  t->elem = elem;
  t->array_kind = kind;
  t->count = count;
  arrays_[key] = t;
  return t;
}

Type* TypeTable::proc(const std::vector<Type*>& params, const std::vector<Type*>& returns, bool c_call,
                      bool variadic) {
  auto key = std::make_tuple(params, returns, c_call, variadic);
  auto it = procs_.find(key);
  if (it != procs_.end()) return it->second;
  const int ps = layout_.pointer_size;
  Type* t = make(TypeKind::Proc, "", ps, ps);
  t->params = params;
  t->returns = returns;
  t->c_call = c_call;
  t->variadic = variadic;
  procs_[key] = t;
  return t;
}

Type* TypeTable::tuple(const std::vector<Type*>& elems) {
  auto it = tuples_.find(elems);
  if (it != tuples_.end()) return it->second;
  Type* t = make(TypeKind::Tuple, "", 0, 1);
  t->elems = elems;
  int64_t off = 0, al = 1;
  for (Type* e : elems) {
    off = align_up(off, e->align) + e->size;
    al = std::max(al, e->align);
  }
  t->size = align_up(off, al);
  t->align = al;
  tuples_[elems] = t;
  return t;
}

Type* TypeTable::new_struct(const std::string& name) { return make(TypeKind::Struct, name, 0, 1); }

Type* TypeTable::new_enum(const std::string& name, Type* base, bool flags) {
  Type* t = make(TypeKind::Enum, name, base->size, base->align);
  t->elem = base;
  t->is_flags = flags;
  t->bits = base->bits;
  t->is_signed = base->is_signed;
  return t;
}

void TypeTable::layout_struct(Type* t) {
  int64_t off = 0, al = 1;
  for (auto& f : t->fields) {
    if (t->is_union) {
      f.offset = 0;
      off = std::max(off, f.type->size);
    } else {
      off = align_up(off, f.type->align);
      f.offset = off;
      off += f.type->size;
    }
    al = std::max(al, f.type->align);
  }
  t->size = align_up(off, al);
  t->align = al;
  t->layout_state = Type::LayoutState::Done;
}

std::string type_name(const Type* t) {
  if (!t) return "<null type>";
  switch (t->kind) {
    case TypeKind::Pointer:
      return "*" + type_name(t->elem);
    case TypeKind::Array:
      switch (t->array_kind) {
        case ArrayKind::Fixed: return "[" + std::to_string(t->count) + "]" + type_name(t->elem);
        case ArrayKind::View: return "[]" + type_name(t->elem);
        case ArrayKind::Dynamic: return "[..]" + type_name(t->elem);
      }
      break;
    case TypeKind::Proc: {
      std::string s = "(";
      for (size_t i = 0; i < t->params.size(); ++i) {
        if (i) s += ", ";
        if (t->variadic && i + 1 == t->params.size()) s += ".." + type_name(t->params[i]->elem);
        else s += type_name(t->params[i]);
      }
      s += ")";
      if (!t->returns.empty()) {
        s += " -> ";
        for (size_t i = 0; i < t->returns.size(); ++i) {
          if (i) s += ", ";
          s += type_name(t->returns[i]);
        }
      }
      if (t->c_call) s += " #c_call";
      return s;
    }
    case TypeKind::Tuple: {
      std::string s = "(";
      for (size_t i = 0; i < t->elems.size(); ++i) {
        if (i) s += ", ";
        s += type_name(t->elems[i]);
      }
      return s + ")";
    }
    default:
      break;
  }
  return t->name;
}

}  // namespace ojay
