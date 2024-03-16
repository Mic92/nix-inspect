#include "inspector.hh"

#include <nix/config.h>

#include <memory>
#include <nix/attr-path.hh>
#include <nix/canon-path.hh>
#include <nix/command.hh>
#include <nix/eval.hh>
#include <nix/nixexpr.hh>
#include <nix/shared.hh>
#include <nix/store-api.hh>
#include <nix/value-to-json.hh>
#include <nix/value.hh>
#include <string>

#include "logging.hh"

const auto MAX_SIZE = 32768;

NixInspector::NixInspector()
    : state(getEvalState().get_ptr().get()),
      vRoot(*state->allocValue()),
      autoArgs(*state->buildBindings(0).finish()) {
  state->eval(
      state->parseExprFromString(
          std::string("builtins.getFlake (toString /etc/nixos)"),
          // std::string("builtins.getFlake \"github:nixos/nixpkgs\""),
          state->rootPath(CanonPath::root)
      ),
      vRoot
  );
}

// void NixInspector::initEnv() {
//   env = &state->allocEnv(MAX_SIZE);
//   env->up = &state->baseEnv;
//   displ = 0;
//   static_env->vars.clear();
//   static_env->sort();
// }

// void NixInspector::addAttrsToScope(Value &attrs) {
//   // state->forceAttrs(
//   //     attrs, [&]() { return attrs.determinePos(noPos); },
//   //     "while evaluating an attribute set to be merged in the global
//   scope"); if (displ + attrs.attrs->size() >= MAX_SIZE)
//     throw Error("environment full; cannot add more variables");
//
//   for (auto i : *attrs.attrs) {
//     static_env->vars.emplace_back(i.name, displ);
//     env->values[displ++] = i.value;
//   }
//   static_env->sort();
//   static_env->deduplicate();
// }
//
// Value NixInspector::_eval(std::string path) {
//   Value vRes;
//   auto expr = state->parseExprFromString(
//       path, state->rootPath(CanonPath::root), static_env
//   );
//
//   expr->eval(*state, *env, vRes);
//
//   return vRes;
// }

std::shared_ptr<Value> NixInspector::inspect(const std::string &attrPath) {
  Value &v(
      *findAlongAttrPath(*state, std::string(attrPath), autoArgs, vRoot).first
  );
  state->forceValue(v, v.determinePos(noPos));
  Value vRes;
  state->autoCallFunction(autoArgs, v, vRes);
  return std::make_shared<Value>(vRes);
}

int32_t NixInspector::v_int(const Value &value) { return value.integer; }
float_t NixInspector::v_float(const Value &value) { return value.fpoint; }
bool NixInspector::v_bool(const Value &value) { return value.boolean; }
std::string NixInspector::v_string(const Value &value) {
  return value.string.c_str;
}
std::string NixInspector::v_path(const Value &value) {
  return value._path.path;
}
nlohmann::json NixInspector::v_repr(const Value &value) {
  switch (value.type()) {
    case nix::nAttrs: {
      auto collected = std::vector<std::string>();
      for (auto x : *value.attrs) {
        auto name = state->symbols[x.name];
        collected.push_back(name);
      }
      return collected;
    }
    case nix::nList: {
      return value.listSize();
    }
    case nix::nString:
      return value.string.c_str;
    case nix::nPath:
      return value._path.path;
    case nix::nBool:
      return value.boolean;
    case nix::nFloat:
      return value.fpoint;
    case nix::nInt:
      return value.integer;
    case nix::nNull:
      return nullptr;
    case nix::nThunk:
    case nix::nFunction:
    case nix::nExternal:
      return nullptr;
  }
}

// std::vector<NixAttr> NixInspector::v_attrs(const Value &value) {
//   auto collected = std::vector<NixAttr>();
//   for (auto x : *value.attrs) {
//     auto name = state->symbols[x.name];
//     auto value = std::make_shared<Value>(*x.value);
//     collected.push_back(NixAttr{.key = (std::string)name, .value = value});
//     ;
//   }
//   return collected;
// }
std::unique_ptr<std::vector<Value>> NixInspector::v_list(const Value &value) {
  auto collected = std::vector<Value>();
  for (auto x : value.listItems()) {
    collected.emplace_back(*x);
  }
  return std::make_unique<std::vector<Value>>(collected);
}
void init_nix_inspector() {
  initNix();
  initGC();
  logger = new CaptureLogger();
}
std::unique_ptr<NixInspector> new_nix_inspector() {
  return std::unique_ptr<NixInspector>(new NixInspector());
}

ValueType NixInspector::v_type(const Value &value) { return value.type(); }

// Gets a attribute at a specific name and if the passed value is a thunk it
// evaluates it SAFETY: this function only safe to call if the value being
// passed is an attrset or a thunk that results in an attrset
std::shared_ptr<Value> NixInspector::v_child(
    const Value &value, std::string key
) {
  auto x = value.attrs->get(state->symbols.create(std::string(key)));
  Value vRes;
  if (x->value->isThunk()) {
    x->value->thunk.expr->eval(*state, *x->value->thunk.env, vRes);
  } else {
    vRes = *x->value;
  }
  return std::make_shared<Value>(vRes);
}
