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
    double effectiveAssets = std::max(0.0, std::min(st.assets, assets));
    double availableCurrency = std::max(0.0, st.currency);

    double cfgInitBet = (std::isnan(cfg.initBet) || cfg.initBet <= 0.0) ? 0 : cfg.initBet;
    double cfgSellStrength = (cfg.sellStrength <= 0.0 || std::isnan(cfg.sellStrength)) ? 0 : cfg.sellStrength;
    double cfgBuyStrength = (cfg.buyStrength <= 0.0 || std::isnan(cfg.buyStrength)) ? 0 : cfg.buyStrength;
    double minAboveEnterPerc = (cfg.minAboveEnter <= 0.0) ? 0 : cfg.minAboveEnter / 100;

    double budget = (std::isnan(st.budget) || st.budget <= 0) ? 0 : st.budget;
    double enterPrice = (std::isnan(st.enter) || std::isinf(st.enter) || st.enter < 0) ? 0 : st.enter;

	double size = 0;
    double sellStrength = 0;
    double buyStrength = 0;
    double distEnter = 0;

    double pnl = (effectiveAssets * price) - (effectiveAssets * enterPrice);
    double initialBetSize = ((cfgInitBet/ 100) * budget) / price;

    bool alert = false;
    bool downtrend = (minAboveEnterPerc == 0.0) ? false : true;

	if (enterPrice == 0 || effectiveAssets < minSize) { // effectiveAssets < ((cfgInitBet/ 100) * st.budget) / price
        size = (initialBetSize > minSize && dir > 0.0) ? initialBetSize : minSize;

        if (price > st.last_price) {alert = !downtrend; size = 0;}
        else if (st.sentiment > 0 && !downtrend) {alert = true;size = 0;} 
        else {size = (st.alerts > 0) ? ((size / st.alerts) < minSize ? minSize : size / 2) : size;} // deleno st.alerts funguje zvlastne, lepe funguje / 2

		// if (price > st.last_price) {
		// 	// Move last price up with alert, unless downtrend mode is enabled
        //     alert = !downtrend;
		// 	size = 0;
		// } else if (st.sentiment > 0 && !downtrend) {
		// 	// Move last price up or down with alert due to uptrend sentiment
		// 	alert = true;
		// 	size = 0;
		// } else {
        //     size = (st.alerts > 0) ? ((size / st.alerts) < minSize ? minSize : size / 2) : size; // deleno st.alerts funguje zvlastne, lepe funguje / 2
		// }
	} else {
        //Turn off alerts for opposite directions. Do not calculate the strategy = useless.
        if (dir > 0 && enterPrice < price) { size = 0; return {size, alert};}
        if (dir < 0 && enterPrice > price) { size = 0; return {size, alert};}

        //Enter price distance, calculation
        distEnter = (enterPrice > price) ? (enterPrice - price) / enterPrice : (price - enterPrice) / price;
        distEnter = (distEnter > 1) ? 1 : distEnter; // <- Muze byt vetsi jak 100% u hyperSracek.

        cfgSellStrength = (cfgSellStrength >= 1) ? 1 : cfgSellStrength;
        cfgBuyStrength = (cfgBuyStrength >= 1) ? 1 : cfgBuyStrength;

        

        //Parabola + Sinus - Srdce strategie.
        buyStrength = (cfgBuyStrength == 0.0 || cfgBuyStrength >= 1) ? std::sin(std::pow(distEnter, 2) * (M_PI / 2)) : (std::sin(std::pow(distEnter, 2)) / std::pow(1 - cfg.buyStrength, 4));    
        sellStrength = (cfgSellStrength >= 1) ? 1 : std::sin(std::pow(distEnter, 2) + M_PI) / std::pow(1 - cfg.sellStrength, 4) + 1;

        //Decision making process. How much to hold when buying/selling.
        double assetsToHoldWhenBuying = 0;
        double assetsToHoldWhenSelling = 0;
        double sentimentDenominator = (st.sentiment <= -1) ? std::abs(st.sentiment) : 1; // Nevim jak to realne pouzit ? 

        assetsToHoldWhenBuying = ((budget * buyStrength) / price) / sentimentDenominator; //enterPrice
        assetsToHoldWhenSelling = (cfgSellStrength <= 0) ? effectiveAssets : (budget * sellStrength) / price; //Never Sell
        
        if (dir > 0 && enterPrice > price) {
            size = std::max(0.0, std::min(assetsToHoldWhenBuying - effectiveAssets, availableCurrency / price));
            size = (size < minSize) ? 0 : size;
        }

        if (dir < 0 && enterPrice + (enterPrice * minAboveEnterPerc) < price) {
            size = std::max(0.0, std::min(std::abs(assetsToHoldWhenSelling - effectiveAssets), effectiveAssets));
            size = (size < minSize) ? 0 : size;
            size = (cfgSellStrength >= 1) ? effectiveAssets : size;
            size = size * -1;
        }

        //Do not sell if in Loss.
        if (pnl < 0 && dir < 0) { size = 0; }
    }

    return {size, alert};
}

IStrategy::OrderData Strategy_Mca::getNewOrder(
const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
double dir, double assets, double currency, bool rej) const {

	// logInfo("getNewOrder: new_price=$1, assets=$2, currency=$3, dir=$4", new_price, assets, currency, dir);

    double _minSize = minSize(minfo, new_price);
    auto res = calculateSize(new_price, assets, dir, _minSize);
	double size = res.first;
	auto alert = res.second ? IStrategy::Alert::enabled : IStrategy::Alert::disabled;

	// logInfo("   -> $1 (alert: $2)", size, res.second);
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

	// logInfo("onTrade: tradeSize=$1, assetsLeft=$2, currencyLeft=$3, enterPrice=$4", tradeSize, assetsLeft, currencyLeft, st.enter);

    auto newAsset = ((st.assets + effectiveSize) < 0) ? 0 : st.assets + effectiveSize;

    auto cost = tradePrice * effectiveSize;
	auto norm_profit = (effectiveSize >= 0) ? 0 : (tradePrice - st.enter) * -effectiveSize;
	auto ep = (effectiveSize >= 0) ? st.ep + cost : (st.ep / st.assets) * newAsset;
	auto enter = ep / newAsset;
	auto alerts = tradeSize == 0 ? (st.alerts + 1) : 0;
	long dir = tradeSize > 0 ? -1 : (tradeSize < 0 ? 1 : (tradePrice > st.last_price ? 1 : -1));
	long sentiment = st.history[0] + st.history[1] + st.history[2] + st.history[3] + st.history[4] + st.history[5] + dir;

	// logInfo("onTrade: tradeSize=$1, assetsLeft=$2, enter=$3, currencyLeft=$4", tradeSize, assetsLeft, enter, currencyLeft);

	return {
		// norm. p, accum, neutral pos, open price
		{ norm_profit, 0, std::isnan(enter) ? tradePrice : enter, 0 },
		PStrategy(new Strategy_Mca(cfg, State { ep, enter, st.budget, newAsset, std::min(st.budget, st.currency - cost), tradePrice, alerts, 
			{ st.history[1], st.history[2], st.history[3], st.history[4], st.history[5], dir }, sentiment }))
	};
}

PStrategy Strategy_Mca::importState(json::Value src, const IStockApi::MarketInfo &minfo) const {
    auto h = src["history"];
	State st {
			src["ep"].getNumber(),
			src["enter"].getNumber(),
			src["budget"].getNumber(),
			src["assets"].getNumber(),
			src["currency"].getNumber(),
			src["last_price"].getNumber(),
			src["alerts"].getInt(),
			{ h[0].getInt(), h[1].getInt(), h[2].getInt(), h[3].getInt(), h[4].getInt(), h[5].getInt() },
			src["sentiment"].getInt()
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
		{"alerts", st.alerts},
		{"history", {st.history[0],st.history[1],st.history[2],st.history[3],st.history[4],st.history[5]}},
		{"sentiment", st.sentiment}
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
    double effectiveAssets = std::max(0.0, std::min(st.assets, assets));
    double minSize = (st.budget / lastPrice) * 0.01;
    double enterPrice = (std::isnan(st.enter) || std::isinf(st.enter) || st.enter < 0) ? 0 : st.enter;
    double enter = 0;

    enter = (enterPrice == 0 || effectiveAssets < minSize) ? lastPrice : st.enter;

    double cp = enter;

	// logInfo("getCenterPrice: lastPrice=$1, assets=$2 -*> $3", lastPrice, assets, cp);
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
		{"Currency", st.currency},
		{"Last price", st.last_price},
		{"Alert count", st.alerts},
		{"Market history", {st.history[0],st.history[1],st.history[2],st.history[3],st.history[4],st.history[5]}},
		{"Market sentiment", st.sentiment}
	};
}