#include "OurSCCPPass.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"
using namespace llvm;

namespace {

struct OurSCCPPass : public FunctionPass {
    static char ID;
    OurSCCPPass() : FunctionPass(ID) {}

    std::unordered_map<Value *, LatticeVal> LatticeMap;

    struct PairHash {
        size_t operator()(const std::pair<BasicBlock *, BasicBlock *> &p) const {
            return std::hash<void *>()(p.first) ^ (std::hash<void *>()(p.second) << 1);
        }
    };
    std::unordered_set<std::pair<BasicBlock *, BasicBlock *>, PairHash> ExecEdges;

    std::queue<std::pair<BasicBlock *, BasicBlock *>> CFGWorklist;

    std::queue<Value *> SSAWorklist;

    LatticeVal &getLattice(Value *V) {
        auto [it, inserted] = LatticeMap.emplace(V, LatticeVal{});
        if (inserted) {
            if (Constant *C = dyn_cast<Constant>(V))
                it->second.markConstant(C);
        }
        return it->second;
    }

    /*
     * markOverdefined
     * Moves V's lattice entry to OVERDEFINED.
     * If the state actually changed, pushes V onto the SSA worklist so that
     * all instructions that use V will be re-evaluated.
     */
    void markOverdefined(Value *V) {
        if (getLattice(V).markOverdefined()) {
            errs() << "[SCCP] markOverdefined: " << V->getName() << "\n";
            SSAWorklist.push(V);
        }
    }

    void markConstant(Value *V, Constant *C) {
        if (getLattice(V).markConstant(C)) {
            errs() << "[SCCP] markConstant: " << V->getName()
                   << " = " << *C << "\n";
            SSAWorklist.push(V);
        }
    }

    bool markEdgeExecutable(BasicBlock *Src, BasicBlock *Dst) {
        if (!ExecEdges.emplace(Src, Dst).second)
            return false; // edge was already executable
        errs() << "[SCCP] markEdgeExecutable: "
               << (Src ? Src->getName() : StringRef("ENTRY"))
               << " -> " << Dst->getName() << "\n";
        CFGWorklist.emplace(Src, Dst);
        return true;
    }

    bool isEdgeExecutable(BasicBlock *Src, BasicBlock *Dst) const {
        return ExecEdges.count({Src, Dst}) > 0;
    }

    bool isBlockExecutable(BasicBlock *BB) const {
        if (ExecEdges.count({nullptr, BB})) return true;
        for (auto *P : predecessors(BB))
            if (isEdgeExecutable(P, BB)) return true;
        return false;
    }

    unsigned executablePredecessorCount(BasicBlock *BB) const {
        unsigned cnt = ExecEdges.count({nullptr, BB}) ? 1 : 0;
        for (auto *P : predecessors(BB))
            if (isEdgeExecutable(P, BB)) cnt++;
        return cnt;
    }

    void visitPHI(PHINode *Phi) {
        errs() << "[SCCP] visitPHI: " << Phi->getName()
               << " in " << Phi->getParent()->getName() << "\n";

        LatticeVal &Result = getLattice(Phi);
        if (Result.isOverdefined()) return; // already at top, nothing to do

        for (unsigned i = 0, e = Phi->getNumIncomingValues(); i < e; ++i) {
            BasicBlock *InBB  = Phi->getIncomingBlock(i);
            Value      *InVal = Phi->getIncomingValue(i);

            if (!isEdgeExecutable(InBB, Phi->getParent())) {
                errs() << "[SCCP]   skip incoming from " << InBB->getName()
                       << " (edge not executable)\n";
                continue;
            }

            LatticeVal &InLV = getLattice(InVal);
            errs() << "[SCCP]   incoming from " << InBB->getName()
                   << " state=" << InLV.state << "\n";

            if (InLV.isOverdefined()) {
                markOverdefined(Phi);
                return;
            }
            if (InLV.isConstant()) {
                if (getLattice(Phi).markConstant(InLV.ConstVal)) {
                    errs() << "[SCCP]   PHI " << Phi->getName()
                           << " updated to " << *InLV.ConstVal << "\n";
                    SSAWorklist.push(Phi);
                }
                if (getLattice(Phi).isOverdefined()) return;
            }

        }
    }

    void visitBinaryOp(BinaryOperator *BI, const DataLayout &DL) {
        errs() << "[SCCP] visitBinaryOp: " << BI->getName()
               << " (" << BI->getOpcodeName() << ")\n";

        LatticeVal &LHS = getLattice(BI->getOperand(0));
        LatticeVal &RHS = getLattice(BI->getOperand(1));

        if (LHS.isOverdefined() || RHS.isOverdefined()) {
            markOverdefined(BI);
            return;
        }
        if (LHS.isConstant() && RHS.isConstant()) {
            if (Constant *Folded = ConstantFoldInstruction(BI, DL)) {
                errs() << "[SCCP]   folded to " << *Folded << "\n";
                markConstant(BI, Folded);
                return;
            }

            markOverdefined(BI);
            return;
        }
        errs() << "[SCCP]   waiting (operand(s) undef)\n";
    }

    void visitCmpInst(CmpInst *CI, const DataLayout &DL) {
        errs() << "[SCCP] visitCmpInst: " << CI->getName() << "\n";

        LatticeVal &LHS = getLattice(CI->getOperand(0));
        LatticeVal &RHS = getLattice(CI->getOperand(1));

        if (LHS.isOverdefined() || RHS.isOverdefined()) {
            markOverdefined(CI);
            return;
        }
        if (LHS.isConstant() && RHS.isConstant()) {
            if (Constant *Folded = ConstantFoldInstruction(CI, DL)) {
                errs() << "[SCCP]   comparison folded to " << *Folded << "\n";
                markConstant(CI, Folded);
                return;
            }
            markOverdefined(CI);
            return;
        }
        errs() << "[SCCP]   comparison waiting (operand(s) undef)\n";
    }

    void visitCastInst(CastInst *Cast,const DataLayout &DL) {
        errs() << "[SCCP] visitCastInst: " << Cast->getName() << "\n";

        LatticeVal &Op = getLattice(Cast->getOperand(0));
        if (Op.isOverdefined()) { markOverdefined(Cast); return; }
        if (Op.isConstant()) {
            if (Constant *Folded = ConstantFoldInstruction(Cast, DL)) {
                errs() << "[SCCP]   cast folded to " << *Folded << "\n";
                markConstant(Cast, Folded);
                return;
            }
            markOverdefined(Cast);
            return;
        }
        errs() << "[SCCP]   cast operand still undef\n";
    }

    void visitTerminator(Instruction *TI, BasicBlock *BB) {
        if (auto *BI = dyn_cast<BranchInst>(TI)) {
            if (BI->isUnconditional()) {
                errs() << "[SCCP] unconditional branch: "
                       << BB->getName() << " -> "
                       << BI->getSuccessor(0)->getName() << "\n";
                markEdgeExecutable(BB, BI->getSuccessor(0));
                return;
            }

            Value      *Cond   = BI->getCondition();
            LatticeVal &CondLV = getLattice(Cond);
            errs() << "[SCCP] conditional branch in " << BB->getName()
                   << " cond=" << Cond->getName()
                   << " state=" << CondLV.state << "\n";

            if (CondLV.isUndef()) {
                errs() << "[SCCP]   condition undef - waiting\n";
                return;
            }
            if (CondLV.isOverdefined()) {
                errs() << "[SCCP]   condition overdefined - marking both edges\n";
                markEdgeExecutable(BB, BI->getSuccessor(0));
                markEdgeExecutable(BB, BI->getSuccessor(1));
                return;
            }
            ConstantInt *CI = dyn_cast<ConstantInt>(CondLV.ConstVal);
            if (!CI) {
                markEdgeExecutable(BB, BI->getSuccessor(0));
                markEdgeExecutable(BB, BI->getSuccessor(1));
                return;
            }
            bool condTrue = CI->isOne();
            errs() << "[SCCP]   condition is constant " << CI->getZExtValue()
                   << " -> taking " << (condTrue ? "true(0)" : "false(1)")
                   << " branch\n";
            markEdgeExecutable(BB, BI->getSuccessor(condTrue ? 0 : 1));
            return;
        }

        if (auto *SI = dyn_cast<SwitchInst>(TI)) {
            Value      *Cond   = SI->getCondition();
            LatticeVal &CondLV = getLattice(Cond);
            if (CondLV.isUndef()) return;
            if (CondLV.isOverdefined()) {
                for (auto &Case : SI->cases())
                    markEdgeExecutable(BB, Case.getCaseSuccessor());
                markEdgeExecutable(BB, SI->getDefaultDest());
                return;
            }
            ConstantInt *CV = dyn_cast<ConstantInt>(CondLV.ConstVal);
            if (!CV) {
                for (auto &Case : SI->cases())
                    markEdgeExecutable(BB, Case.getCaseSuccessor());
                markEdgeExecutable(BB, SI->getDefaultDest());
                return;
            }
            for (auto &Case : SI->cases()) {
                if (Case.getCaseValue() == CV) {
                    errs() << "[SCCP] switch constant " << *CV
                           << " -> case " << Case.getCaseSuccessor()->getName() << "\n";
                    markEdgeExecutable(BB, Case.getCaseSuccessor());
                    return;
                }
            }
            errs() << "[SCCP] switch constant " << *CV << " -> default\n";
            markEdgeExecutable(BB, SI->getDefaultDest());
            return;
        }
    }

    void visitNonPHI(Instruction *I,const DataLayout &DL) {
        if (auto *BI = dyn_cast<BinaryOperator>(I)) {
            visitBinaryOp(BI, DL);
            return;
        }

        if (auto *CI = dyn_cast<CmpInst>(I)) {
            visitCmpInst(CI, DL);
            return;
        }

        if (auto *Cast = dyn_cast<CastInst>(I)) {
            visitCastInst(Cast, DL);
            return;
        }

        if (I->isTerminator())                         { return; }

        if (I->getType()->isVoidTy()) return;

        errs() << "[SCCP] conservative overdefined: ";
        I->print(errs()); errs() << "\n";
        markOverdefined(I);
    }

    void runSCCP(Function &F) {
        errs() << "\n=== SCCP phase: function " << F.getName() << " ===\n";
        const DataLayout &DL = F.getParent()->getDataLayout();
        markEdgeExecutable(nullptr, &F.getEntryBlock());

        for (auto &Arg : F.args()) {
            errs() << "[SCCP] argument " << Arg.getName() << " -> overdefined\n";
            markOverdefined(&Arg);
        }

        while (!CFGWorklist.empty() || !SSAWorklist.empty()) {

            while (!CFGWorklist.empty()) {
                auto [Src, Dst] = CFGWorklist.front();
                CFGWorklist.pop();

                unsigned execPreds = executablePredecessorCount(Dst);
                errs() << "[SCCP] CFG edge -> " << Dst->getName()
                       << " (execPreds now=" << execPreds << ")\n";

                for (auto &InstRef : *Dst) {
                    if (!isa<PHINode>(&InstRef)) break;
                    visitPHI(cast<PHINode>(&InstRef));
                }

                if (execPreds == 1) {
                    for (auto &InstRef : *Dst) {
                        Instruction *I = &InstRef;
                        if (isa<PHINode>(I)) continue;
                        if (I->isTerminator())
                            visitTerminator(I, Dst);
                        else
                            visitNonPHI(I,DL);
                    }
                }
            }

            while (!SSAWorklist.empty()) {
                Value *V = SSAWorklist.front();
                SSAWorklist.pop();
                errs() << "[SCCP] SSA edge: " << V->getName() << " changed\n";

                for (auto *User : V->users()) {
                    Instruction *UI = dyn_cast<Instruction>(User);
                    if (!UI) continue;

                    BasicBlock *UBB = UI->getParent();

                    if (PHINode *Phi = dyn_cast<PHINode>(UI)) {
                        visitPHI(Phi);
                    } else if (isBlockExecutable(UBB)) {
                        if (UI->isTerminator())
                            visitTerminator(UI, UBB);
                        else
                            visitNonPHI(UI,DL);
                    }
                }
            }
        }

        errs() << "=== SCCP phase done ===\n\n";
    }

    bool applyConstants(Function &F) {
        errs() << "=== Applying constants ===\n";
        bool Changed = false;
        std::vector<Instruction *> ToDelete;

        for (auto &BB : F) {
            if (!isBlockExecutable(&BB)) {
                errs() << "[SCCP] block " << BB.getName()
                       << " unreachable - skip replacement\n";
                continue;
            }

            for (auto &InstRef : BB) {
                Instruction *I = &InstRef;

                if (auto *BI = dyn_cast<BranchInst>(I)) {
                    if (BI->isConditional()) {
                        LatticeVal &CV = getLattice(BI->getCondition());
                        if (CV.isConstant()) {
                            ConstantInt *CI = dyn_cast<ConstantInt>(CV.ConstVal);
                            if (CI) {
                                bool taken      = CI->isOne();
                                BasicBlock *TakenBB = BI->getSuccessor(taken ? 0 : 1);
                                BasicBlock *DeadBB  = BI->getSuccessor(taken ? 1 : 0);
                                errs() << "[SCCP] folding branch in " << BB.getName()
                                       << ": always -> " << TakenBB->getName()
                                       << "  (dead: " << DeadBB->getName() << ")\n";

                                DeadBB->removePredecessor(&BB);
                                BranchInst::Create(TakenBB, BI);
                                ToDelete.push_back(BI);
                                Changed = true;
                                continue;
                            }
                        }
                    }
                    continue;
                }

                if (I->isTerminator()) continue;
                if (I->getType()->isVoidTy()) continue;

                LatticeVal &LV = getLattice(I);
                if (LV.isConstant()) {
                    errs() << "[SCCP] replace " << I->getName()
                           << " with constant " << *LV.ConstVal << "\n";
                    I->replaceAllUsesWith(LV.ConstVal);
                    ToDelete.push_back(I);
                    Changed = true;
                }
            }
        }

        for (Instruction *I : ToDelete)
            I->eraseFromParent();

        errs() << "=== Constants applied ===\n\n";
        return Changed;
    }

    void printIR(const Function &F) {
        errs() << "\n--- IR of " << F.getName() << " ---\n";
        for (auto &BB : F) {
            errs() << BB.getName() << ":\n";
            for (auto &I : BB) {
                errs() << '\t';
                I.print(errs());
                errs() << '\n';
            }
        }
        errs() << "--- end IR ---\n\n";
    }

    bool runOnFunction(Function &F) override {
        LatticeMap.clear();
        ExecEdges.clear();
        while (!CFGWorklist.empty()) CFGWorklist.pop();
        while (!SSAWorklist.empty()) SSAWorklist.pop();

        errs() << "\n========================================\n"
               << "OurSCCPPass: " << F.getName() << "\n"
               << "========================================\n";
        printIR(F);


        runSCCP(F);

        bool Changed = applyConstants(F);

        errs() << "\n--- IR after OurSCCPPass (before CFG cleanup) ---\n";
        printIR(F);

        return Changed;
    }
};

}

char OurSCCPPass::ID = 0;
static RegisterPass<OurSCCPPass> X("oursccp", "Our SCCP Pass");
