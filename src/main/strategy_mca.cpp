/* 
* Strategy MathematicalCostAveraging
*
* Created on : 1.5.2022
*     Author : https//github.com/McxCZE
*/

#include <imtjson/object.h>
#include "strategy_mca.h"

#include <cmath>

#include "sgn.h"
#include "../shared/logOutput.h"
using ondra_shared::logInfo;

Strategy_Mca::Strategy_Mca(const Config &cfg):cfg(cfg) {}
Strategy_Mca::Strategy_Mca(const Config &cfg, State &&st):cfg(cfg),st(std::move(st)) {}

std::string_view Strategy_Mca::id = "mathematical_cost_averaging";

PStrategy Strategy_Mca::init(double price, double assets, double currency, bool leveraged) const {
    logInfo("init: price=$1, assets=$2, currency=$3", price, assets, currency);

	// Start with asset allocation as reported by mtrader, assume current price as enter price
	PStrategy out(new Strategy_Mca(cfg, State{
			assets * price, //ep
			assets > 0 ? price : std::numeric_limits<double>::quiet_NaN(), //enter
			currency + (assets * price), // budget
			assets,
			currency
	}));

	if (!out->isValid()) throw std::runtime_error("Unable to initialize strategy - failed to validate state");
	return out;
}

static double minSize(const IStockApi::MarketInfo &minfo, double price) {
	return std::max({
		minfo.min_size,
		minfo.min_volume / price,
		minfo.asset_step
	});
}

std::pair<double, bool> Strategy_Mca::calculateSize(double price, double assets, double dir, double minSize) const {
    double effectiveAssets = std::min(assets, st.assets);
    // double position = st.position;
    double availableCurrency = st.currency;
    double cfgInitBet = cfg.initBet;

    if (cfgInitBet < 0) {cfgInitBet = 0;}
    if (cfgInitBet > 100) {cfgInitBet = 100;}
    if (std::isnan(cfgInitBet)) {cfgInitBet = 0;}

    double initialBet = ((cfgInitBet/ 100) * st.budget) / price;

	double size = 0;
    bool alert = true;

	if (std::isnan(st.enter) || std::isinf(st.enter) || effectiveAssets < minSize) {
        size = minSize;
        if (initialBet > minSize) {size = initialBet;}
        if (dir < 0) { size = 0; }
	} else {
        double distEnter = 0;
        double pnl = (effectiveAssets * price) - (effectiveAssets * st.enter);        

        //Enter price distance, calculation
        if (st.enter > price) { distEnter = (st.enter - price) / st.enter; }
        if (st.enter < price) { distEnter = (price - st.enter) / price; }

        if (distEnter > 1) {distEnter = 1;} // <- Muze byt vetsi jak 100% u hyperSracek. Jak osetrit ?

        double cfgSellStrength = cfg.sellStrength;
        double cfgBuyStrength = cfg.buyStrength;

        if (cfgSellStrength == 0 || cfgSellStrength <= 0 || std::isnan(cfgSellStrength)) 
        {cfgSellStrength = 0;}
        if (cfgBuyStrength == 0 || cfgBuyStrength <= 0 || std::isnan(cfgBuyStrength)) 
        {cfgBuyStrength = 0;}

        if (cfgSellStrength == 1 || cfgSellStrength >= 1) 
        {cfgSellStrength = 1;}
        if (cfgBuyStrength == 1 || cfgBuyStrength >= 1) 
        {cfgBuyStrength = 1;}

        //Parabola
        // double sellStrength = std::pow(distEnter, 2) / (1 - cfg.sellStrength);

        //Parabola + Sinus <- Currently Deployed.
        // double sellStrength = -std::pow(distEnter, std::pow((1 - cfg.sellStrength), 4)) + 1;
        // double buyStrength = std::sin(std::pow(distEnter, 2)) / std::pow(1 - cfg.buyStrength, 4);

        //Sinusoids - Production release.
        double sellStrength = std::sin(std::pow(distEnter, 2) + M_PI) / std::pow(1 - cfg.sellStrength, 4) + 1;
        double buyStrength = std::sin(std::pow(distEnter, 2)) / std::pow(1 - cfg.buyStrength, 4);

        if (buyStrength < 0) {buyStrength = 0;}
        if (buyStrength > 1) {buyStrength = 1;}
        if (std::isnan(buyStrength)) {buyStrength = 0;}

        if (sellStrength < 0) {sellStrength = 0;}
        if (sellStrength > 1) {sellStrength = 1;}
        if (std::isnan(sellStrength)) {sellStrength = 0;}

        // Parabola
        double assetsToHoldWhenBuying = (st.budget * buyStrength) / price; //st.enter
        double assetsToHoldWhenSelling = (st.budget * sellStrength) / price; //st.enter

        // Martingale
        // double assetsToHoldWhenBuying = effectiveAssets * 2;
        // double assetsToHoldWhenSelling = 0;

        if (dir > 0 && st.enter > price) {
            size = assetsToHoldWhenBuying - effectiveAssets;
            if (size < 0) { 
                size = minSize;
                alert = false; 
            }
            if (size * price > availableCurrency) { 
                size = availableCurrency / price;
            }

            // <- Sem if, pokud je asset * price > budget, a je vygenerovany nakupni prikaz = Alert. Dosli prachy.
            // if (size < minSize) { size = 0; alert = false; }
        }

        if (dir < 0 && st.enter < price) {
            size = assetsToHoldWhenSelling - effectiveAssets;

            if (size < 0) { 
                size = 0; // Prodaval pod cenou.
            }

            if (size > effectiveAssets) { size = 0; alert = false; }
            // if (size < minSize) {size = 0; alert = false; }
            // if (size > effectiveAssets) { size = effectiveAssets; }
            size = size * dir;
        }

        //Turn off alerts for opposite directions.
        if (dir > 0 && st.enter < price) { 
            alert = false; 
        }

        if (dir < 0 && st.enter > price) { 
            alert = false; 
        }

        if (pnl < 0 && dir < 0) {
            size = 0; 
            alert = false; 
        }
    }

    return {size, alert};
}

IStrategy::OrderData Strategy_Mca::getNewOrder(
const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
double dir, double assets, double currency, bool rej) const {

	logInfo("getNewOrder: new_price=$1, assets=$2, currency=$3, dir=$4", new_price, assets, currency, dir);
	// double size = calculateSize(new_price, assets, dir);
    // if (assets < minfo.asset_step) { assets = 0; } //strategy Assets correction.

    double _minSize = minSize(minfo, new_price);

    auto res = calculateSize(new_price, assets, dir, _minSize);
	double size = res.first;
	auto alert = res.second ? IStrategy::Alert::enabled : IStrategy::Alert::disabled;

	logInfo("   -> $1 (alert: $2)", size, res.second);
	// price where order is put. If this field is 0, recommended price is used
    // size of the order, +buy, -sell. If this field is 0, the order is not placed
	return { 0, size, alert };
}

std::pair<IStrategy::OnTradeResult, ondra_shared::RefCntPtr<const IStrategy> > Strategy_Mca::onTrade(
const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
double assetsLeft, double currencyLeft) const {
	if (!isValid()) throw std::runtime_error("Strategy is not initialized in onTrade call.");
	auto effectiveSize = tradeSize;

	if (tradeSize > 0 && st.assets > assetsLeft - tradeSize) {
		effectiveSize = std::max(assetsLeft - st.assets, 0.0);
	}

	logInfo("onTrade: tradeSize=$1, assetsLeft=$2, currencyLeft=$3, enterPrice=$4", tradeSize, assetsLeft, currencyLeft, st.enter);

    auto newAsset = st.assets + effectiveSize;
    auto cost = tradePrice * effectiveSize;
	auto norm_profit = effectiveSize >= 0 ? 0 : (tradePrice - st.enter) * -effectiveSize;
	auto ep = effectiveSize >= 0 ? st.ep + cost : (st.ep / st.assets) * newAsset;
	auto enter = ep / newAsset;

	//logInfo("onTrade: tradeSize=$1, assetsLeft=$2, enter=$3, currencyLeft=$4", tradeSize, assetsLeft, enter, currencyLeft);

	return {
		// norm. p, accum, neutral pos, open price
		{ norm_profit, 0, std::isnan(enter) ? tradePrice : enter, 0 },
		PStrategy(new Strategy_Mca(cfg, State { ep, enter, st.budget, newAsset, std::min(st.budget, st.currency - cost), tradePrice }))
	};
}

PStrategy Strategy_Mca::importState(json::Value src, const IStockApi::MarketInfo &minfo) const {
	State st {
			src["ep"].getNumber(),
			src["enter"].getNumber(),
			src["budget"].getNumber(),
			src["assets"].getNumber(),
			src["currency"].getNumber(),
			src["last_price"].getNumber()
	};
	return new Strategy_Mca(cfg, std::move(st));
}

IStrategy::MinMax Strategy_Mca::calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const {
	// for UI
	// calcSafeRange kdyžtak jako min vracej 0 a jako max dej std::numeric_limits<double>::infinite()

	MinMax range;
	range.min = 0;
	range.max = std::numeric_limits<double>::infinity();
	return range;
}

bool Strategy_Mca::isValid() const {
	return st.ep >= 0 && !std::isnan(st.budget) && st.budget > 0;
}

json::Value Strategy_Mca::exportState() const {
	// OK
	return json::Object {
		{"ep", st.ep},
		{"enter", st.enter},
		{"budget", st.budget},
		{"assets", st.assets},
		{"currency", st.currency},
		{"last_price", st.last_price},
	};
}

std::string_view Strategy_Mca::getID() const {
	return id;
}

double Strategy_Mca::getCenterPrice(double lastPrice, double assets) const {

	if (!std::isnan(st.last_price) && st.last_price > 0) {
		lastPrice = st.last_price;
	}

    // Pridano - Useless..
    // double minBudgetThreshold = (assets * lastPrice) / st.budget;
    // double enter = 0;
    // if (std::isnan(st.enter) ||
    //  std::isinf(st.enter) ||
    //  st.enter == 0 ||
    //  minBudgetThreshold <= 0.01) { 
    //     enter = lastPrice; 
    // } else {
    //     enter = st.enter;
    // }

    // double cpEnter = enter;
    // Konec pridavku.
    double cp = lastPrice;

	logInfo("getCenterPrice: lastPrice=$1, assets=$2 -*> $3", lastPrice, assets, cp);
	return cp; // cp
}

double Strategy_Mca::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	// OK
	// double budget = minfo.leverage ? currency : (currency + price * assets);
    // double _minSize = minSize(minfo, price);
    // double initialBet = ((cfg.initBet / 100) * st.budget) / price;
    // if (initialBet > _minSize) {return initialBet;}
	// return _minSize;
    return 0; // <- Always zero? Strategy Logic of CalcSize.
}

IStrategy::BudgetInfo Strategy_Mca::getBudgetInfo() const {
	return {
		st.budget, //calcBudget(st.ratio, st.kmult, st.lastp), // total
		0 //->last price st.assets + calculateSize(new_price, st.assets) //calcPosition(st.ratio, st.kmult, st.lastp) // assets
	};
}

double Strategy_Mca::getEquilibrium(double assets) const {
	// for UI
	return st.enter;
	//return calcEquilibrium(st.ratio, st.kmult, assets);
}

double Strategy_Mca::calcCurrencyAllocation(double price) const {
	// this is allocation that strategy wants for the given price
	return st.budget;
}

IStrategy::ChartPoint Strategy_Mca::calcChart(double price) const {
    double size = 0;

	return ChartPoint{
		false, //true
		st.assets + size, //calcPosition(st.ratio, st.kmult, price),
		st.budget, //calcBudget(st.ratio, st.kmult, price)
	};
}

PStrategy Strategy_Mca::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {
	if (!isValid()) return init(curTicker.last, assets, currency, minfo.leverage != 0);
	else return this;
}

PStrategy Strategy_Mca::reset() const {
	return new Strategy_Mca(cfg);
}

json::Value Strategy_Mca::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	// OK
	return json::Object{
		{"Enter price sum", st.ep},
		{"Enter price", st.enter},
		{"Budget", st.budget},
		{"Assets", st.assets},
		{"Currency", st.currency}
	};
}