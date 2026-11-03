#pragma once

// Shared signature-checked IL2CPP adapter for optional runtime modules. No hardcoded
// offsets, generic async return ABI, global inventory mutation, or JNI calls.
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace localify::managed {
inline thread_local std::string missingSignature;
struct Method { void* pointer; };
struct Api {
    void* (*domain)();
    void** (*assemblies)(void*, size_t*);
    void* (*image)(void*);
    const char* (*imageName)(void*);
    void* (*findClass)(void*, const char*, const char*);
    const Method* (*methods)(void*, void**);
    const char* (*methodName)(const Method*);
    const void* (*returnType)(const Method*);
    const void* (*paramType)(const Method*, uint32_t);
    uint32_t (*paramCount)(const Method*);
    uint32_t (*flags)(const Method*, uint32_t*);
    char* (*typeName)(const void*);
    void (*free)(void*);
    void* (*objectClass)(void*);
    void* (*parent)(void*);
    void* (*classFromType)(const void*);
    const void* (*classType)(void*);
    void* (*typeObject)(const void*);
    bool (*isValueType)(void*);
    bool (*isEnum)(void*);
    int32_t (*valueSize)(void*, uint32_t*);
    void* (*newObject)(void*);
    void* (*newArray)(void*,uintptr_t);
    void* (*invoke)(const Method*, void*, void**, void**);
    void* (*unbox)(void*);
    uint32_t (*retain)(void*, bool);
    uint32_t (*weak)(void*, bool);
    void* (*target)(uint32_t);
    void (*release)(uint32_t);
    void* (*nestedTypes)(void*, void**);
    const char* (*className)(void*);
    const char* (*classNamespace)(void*);
    void* (*classImage)(void*);
    void* (*field)(void*, const char*);
    const void* (*fieldType)(void*);
    size_t (*fieldOffset)(void*);
    void (*fieldGet)(void*, void*, void*);
    void (*fieldSetObject)(void*, void*, void*);
    int32_t (*stringLength)(void*);
    const char16_t* (*stringChars)(void*);
    void* (*newString)(const char*);
    void* (*exception)(void*, const char*, const char*, const char*);
    void (*raise)(void*);

    void load(void* (*resolve)(const char*)) {
#define MANAGED_LOAD(member, symbol) member = reinterpret_cast<decltype(member)>(resolve(symbol)); if (!member) throw std::runtime_error(symbol " unavailable");
        MANAGED_LOAD(domain, "il2cpp_domain_get")
        MANAGED_LOAD(assemblies, "il2cpp_domain_get_assemblies")
        MANAGED_LOAD(image, "il2cpp_assembly_get_image")
        MANAGED_LOAD(imageName, "il2cpp_image_get_name")
        MANAGED_LOAD(findClass, "il2cpp_class_from_name")
        MANAGED_LOAD(methods, "il2cpp_class_get_methods")
        MANAGED_LOAD(methodName, "il2cpp_method_get_name")
        MANAGED_LOAD(returnType, "il2cpp_method_get_return_type")
        MANAGED_LOAD(paramType, "il2cpp_method_get_param")
        MANAGED_LOAD(paramCount, "il2cpp_method_get_param_count")
        MANAGED_LOAD(flags, "il2cpp_method_get_flags")
        MANAGED_LOAD(typeName, "il2cpp_type_get_name")
        MANAGED_LOAD(free, "il2cpp_free")
        MANAGED_LOAD(objectClass, "il2cpp_object_get_class")
        MANAGED_LOAD(parent, "il2cpp_class_get_parent")
        MANAGED_LOAD(classFromType, "il2cpp_class_from_type")
        MANAGED_LOAD(classType, "il2cpp_class_get_type")
        MANAGED_LOAD(typeObject, "il2cpp_type_get_object")
        MANAGED_LOAD(isValueType, "il2cpp_class_is_valuetype")
        MANAGED_LOAD(isEnum, "il2cpp_class_is_enum")
        MANAGED_LOAD(valueSize, "il2cpp_class_value_size")
        MANAGED_LOAD(newObject, "il2cpp_object_new")
        MANAGED_LOAD(newArray, "il2cpp_array_new")
        MANAGED_LOAD(invoke, "il2cpp_runtime_invoke")
        MANAGED_LOAD(unbox, "il2cpp_object_unbox")
        MANAGED_LOAD(retain, "il2cpp_gchandle_new")
        MANAGED_LOAD(weak, "il2cpp_gchandle_new_weakref")
        MANAGED_LOAD(target, "il2cpp_gchandle_get_target")
        MANAGED_LOAD(release, "il2cpp_gchandle_free")
        MANAGED_LOAD(nestedTypes, "il2cpp_class_get_nested_types")
        MANAGED_LOAD(className, "il2cpp_class_get_name")
        MANAGED_LOAD(classNamespace, "il2cpp_class_get_namespace")
        MANAGED_LOAD(classImage, "il2cpp_class_get_image")
        MANAGED_LOAD(field, "il2cpp_class_get_field_from_name")
        MANAGED_LOAD(fieldType, "il2cpp_field_get_type")
        MANAGED_LOAD(fieldOffset, "il2cpp_field_get_offset")
        MANAGED_LOAD(fieldGet, "il2cpp_field_get_value")
        MANAGED_LOAD(fieldSetObject, "il2cpp_field_set_value_object")
        MANAGED_LOAD(stringLength, "il2cpp_string_length")
        MANAGED_LOAD(stringChars, "il2cpp_string_chars")
        MANAGED_LOAD(newString, "il2cpp_string_new")
        MANAGED_LOAD(exception, "il2cpp_exception_from_name_msg")
        MANAGED_LOAD(raise, "il2cpp_raise_exception")
#undef MANAGED_LOAD
    }

    void* klass(const char* ns, const char* name, const char* assembly = "Assembly-CSharp.dll") const {
        size_t count = 0;
        void* d = domain();
        void** list = d ? assemblies(d, &count) : nullptr;
        for (size_t i = 0; list && i < count; ++i) {
            void* img = image(list[i]);
            const char* label = imageName(img);
            if (label && std::strcmp(label, assembly) == 0) return findClass(img, ns, name);
        }
        return nullptr;
    }
    std::string type(const void* t) const {
        char* raw = t ? typeName(t) : nullptr;
        std::string result = raw ? raw : "";
        if (raw) free(raw);
        return result;
    }
    bool matches(const void* t, const std::string& expected) const {
        if (expected == "@predicate") {
            const auto name=type(t);
            return name.rfind("System.Predicate<",0)==0 || name.rfind("System.Predicate`",0)==0 ||
                ((name.rfind("System.Func<",0)==0 || name.rfind("System.Func`",0)==0) && name.size()>=15 && name.substr(name.size()-15)=="System.Boolean>");
        }
        if (expected == "@enumerator") return classFromType(t) != nullptr && type(t) != "System.Void";
        if (expected == "@ref") { void* k = classFromType(t); return k && !isValueType(k); }
        if (expected == "@enum") { uint32_t a = 0; void* k = classFromType(t); return k && isEnum(k) && valueSize(k, &a) == 4; }
        return type(t) == expected;
    }
    const Method* find(void* k, const char* name, const std::string& result,
                       std::initializer_list<std::string> params = {}, bool isStatic = false) const {
        const auto label=std::string(k?className(k):"<missing class>")+"."+name;
        std::string candidates;
        for (; k; k = parent(k)) {
            void* it = nullptr;
            while (auto* m = methods(k, &it)) {
                if (!m->pointer || std::strcmp(methodName(m), name) || paramCount(m) != params.size()) continue;
                candidates+=" (";
                for(uint32_t i=0;i<paramCount(m);++i)candidates+=(i?",":"")+type(paramType(m,i));
                candidates+=") -> "+type(returnType(m));
                uint32_t impl = 0;
                if (((flags(m, &impl) & 0x10u) != 0) != isStatic || !matches(returnType(m), result)) continue;
                size_t i = 0; bool ok = true;
                for (const auto& expected : params) if (!matches(paramType(m, i++), expected)) { ok = false; break; }
                if (ok) return m;
            }
        }
        missingSignature=label+candidates;
        return nullptr;
    }
    void* call(const Method* m, void* object, void** args = nullptr) const {
        if (!m) throw std::runtime_error("Managed signature unavailable: "+missingSignature);
        void* error = nullptr;
        void* result = invoke(m, object, args, &error);
        if (error) throw std::runtime_error(std::string("Managed call failed: ") + methodName(m));
        return result;
    }
    void* get(void* object, const char* property, const std::string& result = "@ref") const {
        if (!object) throw std::runtime_error(std::string("Missing object for ") + property);
        return call(find(objectClass(object), (std::string("get_") + property).c_str(), result), object);
    }
    template<class T> T value(void* box) const {
        void* data = box ? unbox(box) : nullptr;
        if (!data) throw std::runtime_error("Missing managed scalar");
        return *static_cast<T*>(data);
    }
    std::string string(void* object, int limit=1024) const {
        if (!object) return {};
        int n = stringLength(object); const char16_t* s = stringChars(object);
        if (n < 0 || n > limit || (!s && n)) throw std::runtime_error("Invalid managed string");
        std::string out;
        for (int i = 0; i < n; ++i) {
            uint32_t c = s[i];
            if (c >= 0xd800 && c <= 0xdbff && i+1 < n && s[i+1] >= 0xdc00 && s[i+1] <= 0xdfff)
                c = 0x10000 + ((c-0xd800)<<10) + s[++i]-0xdc00;
            else if (c >= 0xd800 && c <= 0xdfff) throw std::runtime_error("Invalid UTF-16");
            if (c < 0x80) out += char(c);
            else if (c < 0x800) { out += char(0xc0|(c>>6)); out += char(0x80|(c&63)); }
            else if (c < 0x10000) { out += char(0xe0|(c>>12)); out += char(0x80|((c>>6)&63)); out += char(0x80|(c&63)); }
            else { out += char(0xf0|(c>>18)); out += char(0x80|((c>>12)&63)); out += char(0x80|((c>>6)&63)); out += char(0x80|(c&63)); }
        }
        return out;
    }
    std::string text(void* object, const char* property) const { return string(get(object, property, "System.String")); }
};

class Root {
    const Api* api_;
    uint32_t handle_;
public:
    Root(const Api& api, void* object = nullptr) : api_(&api), handle_(object ? api.retain(object, false) : 0) {
        if (object && !handle_) throw std::runtime_error("Cannot retain managed object");
    }
    ~Root() { if (handle_) api_->release(handle_); }
    Root(const Root&) = delete;
    Root& operator=(const Root&) = delete;
    Root(Root&& other) noexcept : api_(other.api_),handle_(other.handle_) { other.handle_=0; }
    Root& operator=(Root&& other) noexcept {
        if(this!=&other){if(handle_)api_->release(handle_);api_=other.api_;handle_=other.handle_;other.handle_=0;}
        return *this;
    }
    void* get() const { return handle_ ? api_->target(handle_) : nullptr; }
};
}
