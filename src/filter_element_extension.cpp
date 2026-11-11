#define DUCKDB_EXTENSION_MAIN

#include "filter_element_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"

#include "element_filter_eval.hpp"


namespace duckdb {

inline void FilterElementScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &filter_vec = args.data[0];
	auto &codec_vec = args.data[1];

	BinaryExecutor::Execute<string_t, string_t, bool>(
		filter_vec, codec_vec, result, args.size(),
		[&](string_t filter_json, string_t codec_json) -> bool {
			return ElementFilterEval::Match(filter_json.GetString(), codec_json.GetString());
		}
	);
}

static void LoadInternal(ExtensionLoader &loader) {
	// Register a scalar function
	auto quack_scalar_function = ScalarFunction("filter_element",
		{LogicalType::VARCHAR, LogicalType::VARCHAR},
		LogicalType::BOOLEAN,
		FilterElementScalarFun);
	loader.RegisterFunction(quack_scalar_function);
}

void FilterElementExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}
std::string FilterElementExtension::Name() {
	return "filter_element";
}

std::string FilterElementExtension::Version() const {
#ifdef EXT_VERSION_QUACK
	return EXT_VERSION_QUACK;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(filter_element, loader) {
	duckdb::LoadInternal(loader);
}
}
