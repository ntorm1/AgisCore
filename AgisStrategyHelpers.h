#pragma once

#include <memory>
#include <functional>
#include "Asset.h"

AGIS_API extern const std::function<double(double, double)> agis_init;
AGIS_API extern const std::function<double(double, double)> agis_identity;
AGIS_API extern const std::function<double(double, double)> agis_add;
AGIS_API extern const std::function<double(double, double)> agis_subtract;
AGIS_API extern const std::function<double(double, double)> agis_multiply;
AGIS_API extern const std::function<double(double, double)> agis_divide;

AGIS_API typedef std::function<double(
    double a,
    double b
    )> Operation;

AGIS_API typedef const std::function<double(
    const std::shared_ptr<Asset>&,
    const std::string&,
    int
    )> AssetFeatureLambda;

extern AGIS_API const std::function<double(
    const std::shared_ptr<Asset>&,
    const std::string&,
    int
    )> asset_feature_lambda;


extern AGIS_API const std::function<double(
    const std::shared_ptr<Asset>&,
    const std::vector<
        std::pair<Operation,std::function<double(const std::shared_ptr<Asset>&)>>
        >&operations)> asset_feature_lambda_chain;



#undef AGIS_API