#ifndef SRC_MAIN_STRATEGY_MCA_H_
#define SRC_MAIN_STRATEGY_MCA_H_

#include <string_view>
#include <limits>

#include "istrategy.h"

class Strategy_Mca: public IStrategy {
public:

	struct Config {
		double buyStrength;
        double sellStrength;
		double initBet;
		double minPnl;
		bool useSentiment;
	};

	struct State {
		double ep = 0;
		double enter = std::numeric_limits<double>::quiet_NaN();
		double budget = std::numeric_limits<double>::quiet_NaN();
		double assets = 0;
		double currency = 0;
		double last_price = 0;
		long alerts = 0;
		long history [6] = { };
		long sentiment = 0;
	};

	Strategy_Mca(const Config &cfg);
	Strategy_Mca(const Config &cfg, State &&st);

	PStrategy init(double price, double assets, double currency, bool leveraged) const;

	static std::string_view id;

	virtual IStrategy::OrderData getNewOrder(const IStockApi::MarketInfo &minfo,
			double cur_price, double new_price, double dir, double assets,
			double currency, bool rej) const;
	virtual std::pair<IStrategy::OnTradeResult,
			ondra_shared::RefCntPtr<const IStrategy> > onTrade(
			const IStockApi::MarketInfo &minfo, double tradePrice,
			double tradeSize, double assetsLeft, double currencyLeft) const;
	virtual PStrategy importState(json::Value src,
			const IStockApi::MarketInfo &minfo) const;
	virtual IStrategy::MinMax calcSafeRange(const IStockApi::MarketInfo &minfo,
			double assets, double currencies) const;
	virtual bool isValid() const;
	virtual json::Value exportState() const;
	virtual std::string_view getID() const;
	virtual double getCenterPrice(double lastPrice, double assets) const;
	virtual double calcInitialPosition(const IStockApi::MarketInfo &minfo,
			double price, double assets, double currency) const;
	virtual IStrategy::BudgetInfo getBudgetInfo() const;
	virtual double getEquilibrium(double assets) const;
	virtual double calcCurrencyAllocation(double price) const;
	virtual IStrategy::ChartPoint calcChart(double price) const;
	virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo,
			const IStockApi::Ticker &curTicker, double assets,
			double currency) const;
	virtual PStrategy reset() const;
	virtual json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const;

protected:
	Config cfg;
	State st;

	std::pair<double, bool> calculateSize(double price, double assets, double dir, double minSize) const;
};

#endif /* SRC_MAIN_Strategy_MCA_H_ */