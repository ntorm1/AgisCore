#include "pch.h"

#include "AgisStrategyHelpers.h"


const std::function<double(double, double)> agis_init = [](double a, double b) { return b; };
const std::function<double(double, double)> agis_identity = [](double a, double b) { return a; };
const std::function<double(double, double)> agis_add = [](double a, double b) { return a + b; };
const std::function<double(double, double)> agis_subtract = [](double a, double b) { return a - b; };
const std::function<double(double, double)> agis_multiply = [](double a, double b) { return a * b; };
const std::function<double(double, double)> agis_divide = [](double a, double b) { return a / b; };

const std::function<double(
    const std::shared_ptr<Asset>& asset,
    const std::string& col,
    int offset
    )> asset_feature_lambda = [](
        const std::shared_ptr<Asset>& asset,
        const std::string& col,
        int offset
        ) { return asset->get_asset_feature(col, offset); };


const std::function<double(
    const std::shared_ptr<Asset>&,
    const std::vector<
    std::pair<Operation, std::function<double(const std::shared_ptr<Asset>&)>>
    >& operations)> asset_feature_lambda_chain = [](
        const std::shared_ptr<Asset>& asset,
        const std::vector<std::pair<Operation, std::function<double(const std::shared_ptr<Asset>&)>>>& operations
        )
{
    double result = 0;
    for (const auto& operation : operations) {
        const auto& op = operation.first;
        const auto& assetFeatureLambda = operation.second;
        result = op(result, assetFeatureLambda(asset));
    }
    return result;
};