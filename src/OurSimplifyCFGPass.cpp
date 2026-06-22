#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/CFG.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>
#include <queue>
#include <unordered_set>

using namespace llvm;

namespace {

struct OurSimplifyCFGPass : public FunctionPass {
    static char ID;
    OurSimplifyCFGPass() : FunctionPass(ID) {}

    bool eliminateDeadBlocks(Function &F) {
        bool AnyChanged = false;
        bool Changed    = true;

        while (Changed) {
            Changed = false;

            std::vector<BasicBlock *> Dead;
            for (auto &BB : F) {
                if (&BB == &F.getEntryBlock()) continue;
                if (pred_empty(&BB))
                    Dead.push_back(&BB);
            }

            for (BasicBlock *BB : Dead) {
                errs() << "[SimplifyCFG] eliminateDeadBlock: "
                       << BB->getName() << "\n";
                Changed    = true;
                AnyChanged = true;

                // Avoid removing already threaded PHI predecessors
                for (auto *Succ : successors(BB)) {
                    bool StillReferenced = false;
                    for (auto &InstRef : *Succ) {
                        PHINode *Phi = dyn_cast<PHINode>(&InstRef);
                        if (!Phi) break;
                        if (Phi->getBasicBlockIndex(BB) >= 0) {
                            StillReferenced = true;
                            break;
                        }
                    }
                    if (StillReferenced)
                        Succ->removePredecessor(BB);
                }

                for (auto &InstRef : *BB)
                    if (!InstRef.getType()->isVoidTy() && !InstRef.use_empty())
                        InstRef.replaceAllUsesWith(UndefValue::get(InstRef.getType()));

                while (!BB->empty())
                    BB->back().eraseFromParent();

                BB->eraseFromParent();
            }
        }
        return AnyChanged;
    }

    bool eliminateSinglePredPHIs(Function &F) {
        bool Changed = false;

        for (auto &BB : F) {
            if (!BB.getSinglePredecessor()) continue;

            std::vector<PHINode *> Phis;
            for (auto &I : BB) {
                if (PHINode *Phi = dyn_cast<PHINode>(&I))
                    Phis.push_back(Phi);
                else
                    break;
            }

        for (PHINode *Phi : Phis) {

            // Safety check for malformed IR
            if (Phi->getNumIncomingValues() == 1) {
                Value *OnlyVal = Phi->getIncomingValue(0);

                errs() << "[SimplifyCFG] eliminateSinglePredPHI: replacing "
                    << Phi->getName() << " with " << OnlyVal->getName()
                    << " in " << BB.getName() << "\n";

                Phi->replaceAllUsesWith(OnlyVal);
                Phi->eraseFromParent();
                Changed = true;
            }
        }
        }
        return Changed;
    }

    bool mergeBlockIntoPredecessor(Function &F) {
        bool AnyChanged = false;
        bool Changed    = true;

        while (Changed) {
            Changed = false;

            for (auto BBIt = F.begin(); BBIt != F.end(); ++BBIt) {
                BasicBlock *BB = &*BBIt;
                if (BB == &F.getEntryBlock()) continue;

                BasicBlock *Pred = BB->getSinglePredecessor();
                if (!Pred) continue;

                if (Pred->getSingleSuccessor() != BB) continue;

                BranchInst *BI = dyn_cast<BranchInst>(Pred->getTerminator());
                if (!BI || !BI->isUnconditional()) continue;

                if (isa<PHINode>(BB->front())) continue;

                errs() << "[SimplifyCFG] mergeBlockIntoPredecessor: "
                       << BB->getName() << " into " << Pred->getName() << "\n";

                BI->eraseFromParent();

                Pred->splice(Pred->end(), BB);

                BB->replaceAllUsesWith(Pred);
                BB->eraseFromParent();

                Changed    = true;
                AnyChanged = true;
                break; 
            }
        }
        return AnyChanged;
    }

    bool eliminateRedundantBranches(Function &F) {
        bool AnyChanged = false;
        bool Changed    = true;

        while (Changed) {
            Changed = false;

            for (auto &BB : F) {
                if (&BB == &F.getEntryBlock()) continue;

                BranchInst *BI = dyn_cast<BranchInst>(BB.getTerminator());
                if (!BI || !BI->isUnconditional()) continue;
                if (BB.size() != 1) continue;           
                if (isa<PHINode>(BB.front())) continue; 

                BasicBlock *Succ = BI->getSuccessor(0);
                if (Succ == &BB) continue; 

                std::vector<BasicBlock *> Preds;
                for (auto *Pred : predecessors(&BB))
                    Preds.push_back(Pred);

                if (Preds.empty()) continue;

                bool ConflictingPred = false;
                for (auto &InstRef : *Succ) {
                    PHINode *Phi = dyn_cast<PHINode>(&InstRef);
                    if (!Phi) break;

                    int Idx = Phi->getBasicBlockIndex(&BB);
                    if (Idx < 0) continue;

                    Value *ValFromBB = Phi->getIncomingValue(Idx);

                    for (auto *Pred : Preds) {
                        int PredIdx = Phi->getBasicBlockIndex(Pred);
                        if (PredIdx >= 0 && Phi->getIncomingValue(PredIdx) != ValFromBB)
                            ConflictingPred = true;
                    }
                }
                
                if (ConflictingPred) continue;

                errs() << "[SimplifyCFG] eliminateRedundantBranch: threading "
                       << BB.getName() << " -> " << Succ->getName() << "\n";

                for (auto &InstRef : *Succ) {
                    PHINode *Phi = dyn_cast<PHINode>(&InstRef);
                    if (!Phi) break;

                    int Idx = Phi->getBasicBlockIndex(&BB);
                    if (Idx < 0) continue;

                    Value *ValFromBB = Phi->getIncomingValue(Idx);

                    for (auto *Pred : Preds) {                        
                        if (Phi->getBasicBlockIndex(Pred) >= 0) continue;
                        Phi->addIncoming(ValFromBB, Pred);
                    }

                    Phi->removeIncomingValue(Idx, /*DeletePHIIfEmpty=*/false);
                }

                for (auto *Pred : Preds) {
                    Instruction *Term = Pred->getTerminator();
                    for (unsigned i = 0, e = Term->getNumSuccessors(); i < e; ++i)
                        if (Term->getSuccessor(i) == &BB)
                            Term->setSuccessor(i, Succ);
                }

                Changed    = true;
                AnyChanged = true;
                break; 
            }
        }
        return AnyChanged;
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
        errs() << "\n========================================\n"
               << "OurSimplifyCFGPass: " << F.getName() << "\n"
               << "========================================\n";
        printIR(F);

        bool Changed      = false;
        bool LocalChanged = true;

        while (LocalChanged) {
            LocalChanged = false;
            LocalChanged |= eliminateDeadBlocks(F);
            LocalChanged |= eliminateSinglePredPHIs(F);
            // We call eliminateSinglePredPHIs first because merge requires block with no PHI nodes
            LocalChanged |= mergeBlockIntoPredecessor(F);
            LocalChanged |= eliminateRedundantBranches(F);
            Changed |= LocalChanged;
        }

        errs() << "\n--- Final IR after OurSimplifyCFGPass ---\n";
        printIR(F);

        return Changed;
    }
};

}

char OurSimplifyCFGPass::ID = 0;
static RegisterPass<OurSimplifyCFGPass> Y("oursimplifycfg", "Our CFG Simplification Pass");
