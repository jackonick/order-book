#pragma once
#include "exchange.pb.h"
#include "order_book.h"

exchange::MarketDataEvent to_proto(const Event &e);
Event from_proto(const exchange::MarketDataEvent &m);
