#include "element_filter_eval.hpp"
#include <cmath>
#include <yyjson.hpp>

using namespace duckdb_yyjson;

static inline yyjson_val *Y(void *p) {
	return reinterpret_cast<yyjson_val *>(p);
}

static bool ToInt64(const std::string &s, int64_t &out) {
	char *end = nullptr;
	long long v = std::strtoll(s.c_str(), &end, 10);
	if (end == s.c_str() || *end != '\0')
		return false;
	out = static_cast<int64_t>(v);
	return true;
}

static bool ToDouble(const std::string &s, double &out) {
	char *end = nullptr;
	double v = std::strtod(s.c_str(), &end);
	if (end == s.c_str() || *end != '\0')
		return false;
	out = v;
	return true;
}

static bool ToBool(const std::string &s, bool &out) {
	if (s == "true" || s == "TRUE" || s == "1") {
		out = true;
		return true;
	}
	if (s == "false" || s == "FALSE" || s == "0") {
		out = false;
		return true;
	}
	return false;
}

bool ElementFilterEval::Match(const std::string &filter_json, const std::string &codec_json) {
	// 对齐 Java: standardCodeIndex 为空 => false
	if (codec_json.empty())
		return false;

	CodeMap codes;
	if (!ParseStandardCodes(codec_json, codes))
		return false;

	yyjson_read_err err {};
	yyjson_doc *doc = yyjson_read(filter_json.c_str(), filter_json.size(), 0);
	if (!doc)
		return false;

	yyjson_val *root = yyjson_doc_get_root(doc);
	bool ok = EvalFilterNode(root, codes);

	yyjson_doc_free(doc);
	return ok;
}

bool ElementFilterEval::ParseStandardCodes(const std::string &codec_json, CodeMap &out_map) {
	yyjson_read_err err {};
	yyjson_doc *doc = yyjson_read(codec_json.c_str(), codec_json.size(), 0);
	if (!doc)
		return false;

	yyjson_val *root = yyjson_doc_get_root(doc);
	yyjson_val *arr = yyjson_obj_get(root, "root");
	if (!arr || !yyjson_is_arr(arr)) {
		yyjson_doc_free(doc);
		return false;
	}

	yyjson_arr_iter iter;
	yyjson_arr_iter_init(arr, &iter);
	yyjson_val *item = nullptr;

	while ((item = yyjson_arr_iter_next(&iter)) != nullptr) {
		yyjson_val *sk = yyjson_obj_get(item, "sk");
		yyjson_val *dt = yyjson_obj_get(item, "dt");
		yyjson_val *sv = yyjson_obj_get(item, "sv");
		if (!sk || !dt || !sv || !yyjson_is_str(sk) || !yyjson_is_num(dt)) {
			continue;
		}

		StandardCodeValue v;
		v.dt = static_cast<int>(yyjson_get_int(dt));
		std::string key = yyjson_get_str(sk);

		switch (v.dt) {
		case 0: // Text
			if (yyjson_is_str(sv)) {
				v.text_value = yyjson_get_str(sv);
			} else {
				v.text_value = "";
			}
			break;
		case 1: // Int
			if (yyjson_is_int(sv) || yyjson_is_uint(sv)) {
				v.int_value = static_cast<int64_t>(yyjson_get_sint(sv));
			} else if (yyjson_is_str(sv)) {
				int64_t iv = 0;
				if (!ToInt64(yyjson_get_str(sv), iv)) {
					yyjson_doc_free(doc);
					return false;
				}
				v.int_value = iv;
			} else {
				yyjson_doc_free(doc);
				return false;
			}
			break;
		case 2: // Double
			if (yyjson_is_real(sv) || yyjson_is_num(sv)) {
				v.double_value = yyjson_get_real(sv);
			} else if (yyjson_is_str(sv)) {
				double dv = 0.0;
				if (!ToDouble(yyjson_get_str(sv), dv)) {
					yyjson_doc_free(doc);
					return false;
				}
				v.double_value = dv;
			} else {
				yyjson_doc_free(doc);
				return false;
			}
			break;
		case 3: // Bool
			if (yyjson_is_bool(sv)) {
				v.bool_value = yyjson_get_bool(sv);
			} else if (yyjson_is_str(sv)) {
				bool bv = false;
				if (!ToBool(yyjson_get_str(sv), bv)) {
					yyjson_doc_free(doc);
					return false;
				}
				v.bool_value = bv;
			} else {
				yyjson_doc_free(doc);
				return false;
			}
			break;
		default:
			continue;
		}

		out_map[key] = v;
	}

	yyjson_doc_free(doc);
	return true;
}

bool ElementFilterEval::EvalFilterNode(void *node_ptr, const CodeMap &codes) {
	yyjson_val *node = Y(node_ptr);
	if (!node || !yyjson_is_obj(node))
		return false;

	yyjson_val *is_logical = yyjson_obj_get(node, "isLogical");
	bool logical = is_logical && yyjson_is_bool(is_logical) && yyjson_get_bool(is_logical);

	if (!logical) {
		return EvalActionNode(node, codes);
	}

	yyjson_val *logical_type = yyjson_obj_get(node, "logicalType");
	yyjson_val *sub_filters = yyjson_obj_get(node, "subFilters");
	if (!logical_type || !yyjson_is_str(logical_type) || !sub_filters || !yyjson_is_arr(sub_filters)) {
		return false;
	}

	const char *lt = yyjson_get_str(logical_type);
	bool is_or = std::strcmp(lt, "or") == 0;
	bool is_and = std::strcmp(lt, "and") == 0;
	if (!is_or && !is_and)
		return false;

	yyjson_arr_iter iter;
	yyjson_arr_iter_init(sub_filters, &iter);
	yyjson_val *sub = nullptr;

	if (is_or) {
		while ((sub = yyjson_arr_iter_next(&iter)) != nullptr) {
			if (EvalFilterNode(sub, codes))
				return true;
		}
		return false;
	} else {
		while ((sub = yyjson_arr_iter_next(&iter)) != nullptr) {
			if (!EvalFilterNode(sub, codes))
				return false;
		}
		return true;
	}
}

bool ElementFilterEval::EvalActionNode(void *node_ptr, const CodeMap &codes) {
	yyjson_val *node = Y(node_ptr);
	yyjson_val *at = yyjson_obj_get(node, "actionType");
	yyjson_val *ck = yyjson_obj_get(node, "ck");
	yyjson_val *cv = yyjson_obj_get(node, "cv");
	if (!at || !yyjson_is_num(at) || !ck || !yyjson_is_str(ck) || !cv)
		return false;

	int action_type = static_cast<int>(yyjson_get_int(at));
	std::string key = yyjson_get_str(ck);

	// cv 统一转 string（与 Java ElementActionFilter.standardCodeValue 一致）
	std::string value;
	if (yyjson_is_str(cv))
		value = yyjson_get_str(cv);
	else if (yyjson_is_int(cv) || yyjson_is_uint(cv))
		value = std::to_string((long long)yyjson_get_sint(cv));
	else if (yyjson_is_real(cv))
		value = std::to_string(yyjson_get_real(cv));
	else if (yyjson_is_bool(cv))
		value = yyjson_get_bool(cv) ? "true" : "false";
	else
		return false;

	auto it = codes.find(key);

	// 对齐 Java: info == null && Not_Equal => true
	if (it == codes.end()) {
		return action_type == 7; // Not_Equal
	}

	const StandardCodeValue &v = it->second;
	switch (action_type) {
	case 1:
		return ActionEqual(v, value); // Equal
	case 2:
		return ActionGT(v, value); // GT
	case 3:
		return ActionGET(v, value); // GET
	case 4:
		return ActionLT(v, value); // LT
	case 5:
		return ActionLET(v, value); // LET
	case 6:
		return ActionAdhere(v, value); // Adhere
	case 7:
		return ActionNotEqual(v, value); // Not_Equal
	case 8:
		return true; // ALL
	case 0:
	default:
		return false; // None / unknown
	}
}

// ===== 各动作实现（框架版） =====
bool ElementFilterEval::ActionEqual(const StandardCodeValue &v, const std::string &cv) {
	if (v.dt == 0) return v.text_value == cv;
	if (v.dt == 1) { int64_t x; return ToInt64(cv, x) && v.int_value == x; }
	if (v.dt == 2) { double x; return ToDouble(cv, x) && std::fabs(v.double_value - x) < 1e-12; }
	if (v.dt == 3) { bool x; return ToBool(cv, x) && v.bool_value == x; }
	return false;
}

bool ElementFilterEval::ActionNotEqual(const StandardCodeValue &v, const std::string &cv) {
	return !ActionEqual(v, cv);
}

bool ElementFilterEval::ActionGT(const StandardCodeValue &v, const std::string &cv) {
	if (v.dt == 1) { int64_t x; return ToInt64(cv, x) && v.int_value > x; }
	if (v.dt == 2) { double x; return ToDouble(cv, x) && v.double_value > x; }
	return false;
}

bool ElementFilterEval::ActionGET(const StandardCodeValue &v, const std::string &cv) {
	if (v.dt == 1) { int64_t x; return ToInt64(cv, x) && v.int_value >= x; }
	if (v.dt == 2) { double x; return ToDouble(cv, x) && v.double_value >= x; }
	return false;
}

bool ElementFilterEval::ActionLT(const StandardCodeValue &v, const std::string &cv) {
	if (v.dt == 1) { int64_t x; return ToInt64(cv, x) && v.int_value < x; }
	if (v.dt == 2) { double x; return ToDouble(cv, x) && v.double_value < x; }
	return false;
}

bool ElementFilterEval::ActionLET(const StandardCodeValue &v, const std::string &cv) {
	if (v.dt == 1) { int64_t x; return ToInt64(cv, x) && v.int_value <= x; }
	if (v.dt == 2) { double x; return ToDouble(cv, x) && v.double_value <= x; }
	return false;
}


bool ElementFilterEval::ActionAdhere(const StandardCodeValue &, const std::string &) {
	// 与当前 Java 保持一致：未实现 => false
	return false;
}
