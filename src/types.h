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
  Code,          // `Code`: a piece of unchecked syntax (#code), a compile-time value
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
  int place = -1;                 // `#place m;` before this member: it starts at the offset of field `place`
  int64_t align = 0;              // `#align N`: the offset is a multiple of N (0: the type's alignment)
};

// A constant declared in a struct body (`N :: 3;`): no storage, but listed in
// the struct's Type_Info members (flag CONSTANT) at its place among the fields.
struct StructConstant {
  std::string name;
  Type* type = nullptr;      // the constant's type (Type for a type constant)
  size_t field_index = 0;    // how many fields come before it
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
  bool has_place = false;    // some member follows a `#place`: members may overlap
  bool has_align = false;    // some member has `#align` (Struct_Textual_Flags.ALIGNED)
  std::vector<StructConstant> constants;
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

  // `Name :: #type,distinct Base;`: a type of its own with Base's kind and
  // layout (a copy of Base's description), but never implicitly converted.
  Type* distinct_of = nullptr;
  bool isa = false;  // `#type,isa Base`: also converts implicitly to Base (not back)

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
  bool is_f32() const { return kind == TypeKind::Float && bits == 32; }
  // The type a chain of distinct types ends in.
  const Type* underlying() const {
    const Type* t = this;
    while (t->distinct_of) t = t->distinct_of;
    return t;
  }
};

// Whether fields i and j of struct t share bytes (#place).
bool fields_overlap(const Type* t, size_t i, size_t j);

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
  Type* code_t;
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
  // A distinct type: a copy of base's description under a new identity. A
  // struct base must be laid out first (copy_distinct refreshes it).
  Type* new_distinct(const std::string& name, Type* base, bool isa = false);
  static void copy_distinct(Type* t);

  // Computes size/alignment for a struct whose fields are filled in.
  void layout_struct(Type* t);

  const TargetLayout& target() const { return layout_; }
  // The alignment the runtime's heap guarantees (8 on 32-bit targets, 16 on 64-bit).
  int64_t heap_align() const { return 2 * layout_.pointer_size; }

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
