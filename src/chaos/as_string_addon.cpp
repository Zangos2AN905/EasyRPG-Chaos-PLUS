/*
 * Chaos Fork: Minimal AngelScript std::string addon
 *
 * Provides RegisterStdString() to register std::string as a value type
 * in AngelScript. This is a minimal implementation covering what the
 * mod API needs (string literals, assignment, concatenation, comparison).
 *
 * Only compiled when CHAOS_HAS_ANGELSCRIPT is defined.
 */

#ifdef CHAOS_HAS_ANGELSCRIPT

#include <angelscript.h>
#include <string>
#include <new>
#include <cstring>
#include <cassert>

// ---- String factory (AngelScript 2.31+) ----

class ChaosStringFactory : public asIStringFactory {
public:
	const void* GetStringConstant(const char* data, asUINT length) override {
		return new std::string(data, length);
	}

	int ReleaseStringConstant(const void* str) override {
		delete static_cast<const std::string*>(str);
		return 0;
	}

	int GetRawStringData(const void* str, char* data, asUINT* length) const override {
		auto* s = static_cast<const std::string*>(str);
		if (length) *length = static_cast<asUINT>(s->length());
		if (data) std::memcpy(data, s->c_str(), s->length());
		return 0;
	}
};

static ChaosStringFactory g_stringFactory;

// ---- Behaviours ----

static void StringConstruct(std::string* p) {
	new(p) std::string();
}

static void StringCopyConstruct(const std::string& o, std::string* p) {
	new(p) std::string(o);
}

static void StringDestruct(std::string* p) {
	p->~basic_string();
}

// ---- Operators ----

static std::string& StringAssign(const std::string& o, std::string* self) {
	*self = o;
	return *self;
}

static std::string StringAdd(const std::string& a, const std::string& b) {
	return a + b;
}

static std::string& StringAddAssign(const std::string& o, std::string* self) {
	*self += o;
	return *self;
}

static bool StringEquals(const std::string& a, const std::string& b) {
	return a == b;
}

static int StringCompare(const std::string& a, const std::string& b) {
	int cmp = a.compare(b);
	if (cmp < 0) return -1;
	if (cmp > 0) return 1;
	return 0;
}

// ---- Methods ----

static asUINT StringLength(const std::string* s) {
	return static_cast<asUINT>(s->length());
}

static bool StringIsEmpty(const std::string* s) {
	return s->empty();
}

// ---- Registration ----

void RegisterStdString(asIScriptEngine* engine) {
	int r;

	// Register the string type
	r = engine->RegisterObjectType("string", sizeof(std::string),
		asOBJ_VALUE | asGetTypeTraits<std::string>());
	assert(r >= 0);

	// String factory for literals
	r = engine->RegisterStringFactory("string", &g_stringFactory);
	assert(r >= 0);

	// Constructors / destructor
	r = engine->RegisterObjectBehaviour("string", asBEHAVE_CONSTRUCT,
		"void f()", asFUNCTION(StringConstruct), asCALL_CDECL_OBJLAST);
	assert(r >= 0);

	r = engine->RegisterObjectBehaviour("string", asBEHAVE_CONSTRUCT,
		"void f(const string &in)", asFUNCTION(StringCopyConstruct), asCALL_CDECL_OBJLAST);
	assert(r >= 0);

	r = engine->RegisterObjectBehaviour("string", asBEHAVE_DESTRUCT,
		"void f()", asFUNCTION(StringDestruct), asCALL_CDECL_OBJLAST);
	assert(r >= 0);

	// Assignment
	r = engine->RegisterObjectMethod("string", "string &opAssign(const string &in)",
		asFUNCTION(StringAssign), asCALL_CDECL_OBJLAST);
	assert(r >= 0);

	// Concatenation
	r = engine->RegisterObjectMethod("string", "string opAdd(const string &in) const",
		asFUNCTION(StringAdd), asCALL_CDECL_OBJFIRST);
	assert(r >= 0);

	r = engine->RegisterObjectMethod("string", "string &opAddAssign(const string &in)",
		asFUNCTION(StringAddAssign), asCALL_CDECL_OBJLAST);
	assert(r >= 0);

	// Comparison
	r = engine->RegisterObjectMethod("string", "bool opEquals(const string &in) const",
		asFUNCTION(StringEquals), asCALL_CDECL_OBJFIRST);
	assert(r >= 0);

	r = engine->RegisterObjectMethod("string", "int opCmp(const string &in) const",
		asFUNCTION(StringCompare), asCALL_CDECL_OBJFIRST);
	assert(r >= 0);

	// Utility methods
	r = engine->RegisterObjectMethod("string", "uint length() const",
		asFUNCTION(StringLength), asCALL_CDECL_OBJLAST);
	assert(r >= 0);

	r = engine->RegisterObjectMethod("string", "bool isEmpty() const",
		asFUNCTION(StringIsEmpty), asCALL_CDECL_OBJLAST);
	assert(r >= 0);
}

#endif // CHAOS_HAS_ANGELSCRIPT
