#include "order.h"
#include "order-book.h"
#include "matching-engine.h"


void submit_order(Order order) {
    switch (order.type) {
        case Type::Limit:
            /*
                Buy Side (Asks)
                    - Match with best ask price
                    - If conditions not met, store in order book

                Sell Side (Bids)
                    - Match with best buy price
                    - If conditions not met, store in order book
            */
            break;

        case Type::Market:
            /*
                Buy Side (Asks)
                    - Match with the best ask price

                Sell side (Bids)
                    - Match with the best ask price
            
            */
            break;
    }
}

int main() {

}