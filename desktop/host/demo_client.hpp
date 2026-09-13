#pragma once
#include <memory>

#include "exchange_client.hpp"

namespace ob {

/** Fake-market client for trying the app without a server. Selected with OB_DEMO_CLIENT=1. */
std::unique_ptr<ExchangeClient> makeDemoClient();

}  // namespace ob
