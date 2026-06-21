#ifndef OURSCCP_PASS_H
#define OURSCCP_PASS_H

#include "llvm/IR/Constants.h"


using namespace llvm;

struct LatticeVal {
    enum State { UNDEF, CONSTANT, OVERDEFINED } state;
    Constant *ConstVal;

    LatticeVal() : state(UNDEF), ConstVal(nullptr) {}

    bool isUndef()       const { return state == UNDEF; }
    bool isConstant()    const { return state == CONSTANT; }
    bool isOverdefined() const { return state == OVERDEFINED; }

    bool markConstant(Constant *C) {
        if (state == OVERDEFINED) return false;
        if (state == CONSTANT) {
            if (ConstVal == C) return false;
            state    = OVERDEFINED;
            ConstVal = nullptr;
            return true;
        }
        state    = CONSTANT;
        ConstVal = C;
        return true;
    }

    bool markOverdefined() {
        if (state == OVERDEFINED) return false;
        state    = OVERDEFINED;
        ConstVal = nullptr;
        return true;
    }
};

#endif 
