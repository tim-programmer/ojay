// Semantic types. Types are interned: two structurally identical types share
// one Type object, so pointer equality is type identity (structs and enums are
// nominal and never merged).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace llvm {
class Type;
class GlobalVariable;
}  // namespace llvm

namespace ojay {

struct Expr;
struct Decl;

enum class TypeKind {
  Void,
  Bool,
  Int,
  Float,
  String,
  Pointer,
  Array,
  Struct,
  Enum,
  Proc,
  Any,
  TypeT,         // the type of type values ("Type")
  UntypedInt,    // integer constants that have not yet been given a type
  UntypedFloat,  // float constants that have not yet been given a type
  Null,          // the type of `null` before it is converted to a pointer
  Tuple,         // multiple return values
};

enum class ArrayKind { Fixed, View, Dynamic };

struct Type;

struct StructField {
  std::string name;
  Type* type = nullptr;
  int64_t offset = 0;
  Expr* default_value = nullptr;  // constant expression or null
  bool uninit = false;            // `= ---`
  bool is_using = false;          // `using name: T;` - T's members are also members of the enclosing struct
  bool anonymous = false;         // `struct { ... }` / `union { ... }` member without a name: its members are
                                  // accessed as if they were members of the enclosing struct
};

struct EnumMember {
  std::string name;
  __int128 value = 0;
};

struct Type {
  TypeKind kind;
  std::string name;  // display name

  int64_t size = 0;
  int64_t align = 1;

  // Int
  int bits = 0;
  bool is_signed = false;

  // Pointer / Array element; Enum base type
  Type* elem = nullptr;

  // Array
  ArrayKind array_kind = ArrayKind::Fixed;
  int64_t count = 0;

  // Struct
  std::vector<StructField> fields;
  bool is_union = false;
  enum class LayoutState { None, InProgress, Done } layout_state = LayoutState::None;
  Decl* struct_decl = nullptr;  // for diagnostics

  // Enum
  std::vector<EnumMember> members;
  bool is_flags = false;

  // Proc
  std::vector<Type*> params;
  std::vector<Type*> returns;
  bool c_call = false;
  bool variadic = false;  // last param is `..T` (a []T)

  // Tuple
  std::vector<Type*> elems;

  // Backend caches.
  llvm::Type* llvm_type = nullptr;
  llvm::GlobalVariable* type_info = nullptr;

  explicit Type(TypeKind k) : kind(k) {}

  bool is_integer() const { return kind == TypeKind::Int || kind == TypeKind::UntypedInt; }
  bool is_float() const { return kind == TypeKind::Float || kind == TypeKind::UntypedFloat; }
  bool is_numeric() const { return is_integer() || is_float(); }
  bool is_untyped() const {
    return kind == TypeKind::UntypedInt || kind == TypeKind::UntypedFloat || kind == TypeKind::Null;
  }
  bool is_pointer() const { return kind == TypeKind::Pointer; }
  bool is_proc() const { return kind == TypeKind::Proc; }
  bool is_struct() const { return kind == TypeKind::Struct; }
  bool is_enum() const { return kind == TypeKind::Enum; }
  bool is_array() const { return kind == TypeKind::Array; }
  bool is_fixed_array() const { return kind == TypeKind::Array && array_kind == ArrayKind::Fixed; }
  bool is_view() const { return kind == TypeKind::Array && array_kind == ArrayKind::View; }
  bool is_dynamic() const { return kind == TypeKind::Array && array_kind == ArrayKind::Dynamic; }
  // An integer-like value (ints and enums).
  bool is_intlike() const { return is_integer() || is_enum(); }
};

struct TargetLayout {
  int pointer_size = 8;
};

class TypeTable {
 public:
  explicit TypeTable(TargetLayout layout);

  Type* void_t;
  Type* bool_t;
  Type* s8; Type* s16; Type* s32; Type* s64;
  Type* u8; Type* u16; Type* u32; Type* u64;
  Type* f32; Type* f64;
  Type* string_t;
  Type* any_t;
  Type* type_t;
  Type* untyped_int;
  Type* untyped_float;
  Type* null_t;
  Type* void_ptr;

  Type* int_type(int bits, bool is_signed);
  Type* pointer_to(Type* elem);
  Type* array_of(Type* elem, ArrayKind kind, int64_t count = 0);
  Type* proc(const std::vector<Type*>& params, const std::vector<Type*>& returns, bool c_call, bool variadic);
  Type* tuple(const std::vector<Type*>& elems);
  Type* new_struct(const std::string& name);
  Type* new_enum(const std::string& name, Type* base, bool flags);

  // Computes size/alignment for a struct whose fields are filled in.
  void layout_struct(Type* t);

  const TargetLayout& target() const { return layout_; }

 private:
  TargetLayout layout_;
  std::vector<std::unique_ptr<Type>> owned_;
  std::map<Type*, Type*> pointers_;
  std::map<std::tuple<Type*, int, int64_t>, Type*> arrays_;
  std::map<std::tuple<std::vector<Type*>, std::vector<Type*>, bool, bool>, Type*> procs_;
  std::map<std::vector<Type*>, Type*> tuples_;

  Type* make(TypeKind k, const std::string& name, int64_t size, int64_t align);
};

std::string type_name(const Type* t);

}  // namespace ojay
