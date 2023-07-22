#pragma once

#include <memory>
#include <functional>
#include <unordered_map>
#include "Asset.h"

AGIS_API extern const std::function<double(double, double)> agis_init;
AGIS_API extern const std::function<double(double, double)> agis_identity;
AGIS_API extern const std::function<double(double, double)> agis_add;
AGIS_API extern const std::function<double(double, double)> agis_subtract;
AGIS_API extern const std::function<double(double, double)> agis_multiply;
AGIS_API extern const std::function<double(double, double)> agis_divide;


AGIS_API typedef std::function<double(double, double)> AgisOperation;
AGIS_API typedef std::pair<AgisOperation, std::function<double(const std::shared_ptr<Asset>&)>> AssetLambda;
AGIS_API typedef std::vector<AssetLambda> AgisAssetLambdaChain;

enum class AGIS_Function {
    INIT,
    IDENTITY,
    ADD,
    SUBTRACT,
    MULTIPLY,
    DIVIDE
};

extern AGIS_API std::unordered_map<std::string, AgisOperation> agis_function_map;
extern AGIS_API std::vector<std::string> agis_function_strings;



AGIS_API typedef std::function<double(
    double a,
    double b
    )> Operation;

AGIS_API typedef const std::function<double(
    const std::shared_ptr<Asset>&,
    const std::string&,
    int
    )> AssetFeatureLambda;

extern AGIS_API AssetFeatureLambda asset_feature_lambda;


extern AGIS_API const std::function<double(
    const std::shared_ptr<Asset>&,
    const std::vector<
        std::pair<Operation,std::function<double(const std::shared_ptr<Asset>&)>>
        >&operations)> asset_feature_lambda_chain;



#undef AGIS_API