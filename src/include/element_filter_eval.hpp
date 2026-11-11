#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

struct StandardCodeValue {
	// dt: 0=text,1=int,2=double,3=bool
	int dt = -1;

	std::string text_value;
	int64_t int_value = 0;
	double double_value = 0.0;
	bool bool_value = false;
};

class ElementFilterEval {
public:
	static bool Match(const std::string &filter_json, const std::string &codec_json);

private:
	using CodeMap = std::unordered_map<std::string, StandardCodeValue>;

	static bool ParseStandardCodes(const std::string &codec_json, CodeMap &out_map);

	static bool EvalFilterNode(void *node, const CodeMap &codes);
	static bool EvalActionNode(void *node, const CodeMap &codes);

	static bool ActionEqual(const StandardCodeValue &v, const std::string &cv);
	static bool ActionNotEqual(const StandardCodeValue &v, const std::string &cv);
	static bool ActionGT(const StandardCodeValue &v, const std::string &cv);
	static bool ActionGET(const StandardCodeValue &v, const std::string &cv);
	static bool ActionLT(const StandardCodeValue &v, const std::string &cv);
	static bool ActionLET(const StandardCodeValue &v, const std::string &cv);
	static bool ActionAdhere(const StandardCodeValue &v, const std::string &cv);
};
