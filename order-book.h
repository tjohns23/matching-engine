#ifndef ORDER_BOOK_H
#define ORDER_BOOK_H

#include <cstdint>
#include <map>
#include <unordered_map>
#include <list>
#include <vector>

#include "order.h"

struct PriceLevel {
    Price price;
    Qty total_qty = 0;
    std::list<Order> orders;
};


class OrderBook {
    private:
        std::map<Price, PriceLevel, std::greater<Price>> bids;
        std::map<Price, PriceLevel> asks;
        std::unordered_map<OrderId, std::list<Order>::iterator> order_map;
        void add_order(Order order);
        void remove_order(Order order);
        void match_buy(Order& order);
        void match_sell(Order& order);
        void match(Order order);
        

    public:
        void submit_order(Order order);

};



#endif
