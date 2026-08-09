#include "order-book.h"
#include "order.h"
#include <iostream>



void OrderBook::add_order(Order order) {

    switch (order.side) {

    case Side::Buy: {
        PriceLevel& level = bids[order.price];
        level.orders.push_back(order);
        
        // Keep track of the iterator to the inserted element
        auto it = std::prev(level.orders.end());
        order_map[order.id] = it;
        break;
    }

    case Side::Sell: {
        PriceLevel& level = asks[order.price];
        level.orders.push_back(order);

        // Keep track of the iterator to the inserted element
        auto it = std::prev(level.orders.end());
        order_map[order.id] = it;
        break;
    }

    }
    
}

void OrderBook::remove_order(Order order) {
    switch (order.side) {
        
        case Side::Buy: {

            // Get an interator into the order map
            auto order_map_it = order_map.find(order.id);
            if ((order_map_it) == order_map.end()) break;

            // Get an iterator to the order node
            auto order_it = order_map_it->second;
            
            // Get an iterator to the list containing the order and erase it
            auto price_it = bids.find(order.price);
            if (price_it == bids.end()) break;
            price_it->second.orders.erase(order_it);

            if (price_it->second.orders.empty()) {
                bids.erase(price_it);
            }
            
            // Erase the order iterator from the order map
            order_map.erase(order_map_it);
            break;

        }

        case Side::Sell: {

            // Get an iterator into the order map
            auto order_map_it = order_map.find(order.id);
            if ((order_map_it) == order_map.end()) break;

            // Get an iterator to the order node
            auto order_it = order_map_it->second;

            // Get an iterator to the list containing the order and remove
            auto price_it = asks.find(order.price);
            if (price_it == asks.end()) break;
            price_it->second.orders.erase(order_it);

            if (price_it->second.orders.empty()) {
                asks.erase(price_it);
            }

            // Erase the order iterater from the order map
            order_map.erase(order_map_it);
            break;
        }
    }
}

void OrderBook::match_buy(Order& order) {
    switch (order.type) {
        case Type::Market: {
            while (order.remaining_qty > 0 && !asks.empty()) {
                auto best = asks.begin();
                auto& level = best->second;
                auto& resting = level.orders.front();

                Qty fill = std::min(order.remaining_qty, resting.remaining_qty);
                order.remaining_qty -= fill;
                resting.remaining_qty -= fill;

                if (resting.remaining_qty == 0) {
                    remove_order(resting);
                }
            }
            break;
            
        }

        case Type::Limit: {
            while (order.remaining_qty > 0 && !asks.empty()) {
                auto best = asks.begin();
                Price best_ask = best->first;
                if (best_ask > order.price) break; // Best is too expensive

                auto& level = best->second;
                auto& resting = level.orders.front();

                Qty fill = std::min(order.remaining_qty, resting.remaining_qty);
                order.remaining_qty -= fill;
                resting.remaining_qty -= fill;

                if (resting.remaining_qty == 0) {
                    remove_order(resting);
                }
                
            }
            break; 
        }
        
    }
}

void OrderBook::match_sell(Order& order) {
    switch (order.type) {
        case Type::Market: {
            while (order.remaining_qty > 0 && !bids.empty()) {
                auto best = bids.begin();
                auto& level = best->second;
                auto& resting = level.orders.front();

                Qty fill = std::min(order.remaining_qty, resting.remaining_qty);
                order.remaining_qty -= fill;
                resting.remaining_qty -=fill;

                if (resting.remaining_qty == 0) {
                    remove_order(resting);
                }
            }
            break;
        }

        case Type::Limit: {
            while (order.remaining_qty > 0 && !bids.empty()) {
                auto best = bids.begin();
                Price best_bid = best->first;
                if (best_bid < order.price) break; // Best is too cheap

                auto& level = best->second;
                auto& resting = level.orders.front();

                Qty fill = std::min(order.remaining_qty, resting.remaining_qty);
                order.remaining_qty -= fill;
                resting.remaining_qty -= fill;

                if (resting.remaining_qty == 0) {
                    remove_order(resting);
                }
            }
            break;
        }
    
    }
}



void OrderBook::submit_order(Order& order) {
    switch (order.side) {
        case Side::Buy:  match_buy(order); break;
        case Side::Sell: match_sell(order); break;
    }

    if (order.remaining_qty > 0) {
        add_order(order);
    }
    
}


