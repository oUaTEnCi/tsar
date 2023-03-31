#include "tsar/Analysis/PDG.h"
//#include <llvm/Support/Casting.h>
#include <llvm/Analysis/DOTGraphTraitsPass.h>
#include <llvm/Support/GraphWriter.h>
//#include <clang/AST/Expr.h>
//#include <clang/AST/Stmt.h>
#include <clang/AST/Decl.h>
//#include "tsar/Analysis/Clang/SourceCFG.h"
#include "tsar/Support/PassGroupRegistry.h"
#include "tsar/Core/Query.h"
//
#include <iostream>
//


using namespace tsar;
using namespace llvm;
using namespace clang;
using namespace std;

PDG *PDGBuilder::populate(const SourceCFG &_SCFG) {
    SourceCFG SCFG(_SCFG);
    SourceCFGNode *EntryNode;
    if (!SCFG.getStopNode())
        return nullptr;
    mPDG=new tsar::PDG(string(SCFG.getName()));
    EntryNode=&SCFG.emplaceNode(SourceCFGNode::NodeKind::Default);
    SCFG.bindNodes(*EntryNode, *SCFG.getStartNode(), SourceCFGEdge::EdgeKind::True);
    SCFG.bindNodes(*EntryNode, *SCFG.getStopNode(), SourceCFGEdge::EdgeKind::False);
    mPDG->SCFGPD=new PostDomTreeBase<SourceCFGNode>();
    //
    std::cout<<"Pre Calculate!!!\n";
    //
    mPDG->SCFGPD->recalculate(SCFG);
    //llvm::dumpDotGraphToFile(mPDG, "post_dom_tree.dot", "Try 1");
    return mPDG;
}

char PDGPass::ID=0;

INITIALIZE_PASS_BEGIN(PDGPass, "pdg",
    "Program Dependency Graph", false, true)
INITIALIZE_PASS_DEPENDENCY(ClangSourceCFGPass)
INITIALIZE_PASS_END(PDGPass, "pdg",
    "Program Dependency Graph", false, true)

FunctionPass *createPDGPass() { return new PDGPass; }

void PDGPass::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<ClangSourceCFGPass>();
    AU.setPreservesAll();    
}

bool PDGPass::runOnFunction(Function &F) {
    releaseMemory();
    auto &SCFGPass=getAnalysis<ClangSourceCFGPass>();
    mPDG=mPDGBuilder.populate(SCFGPass.getSourceCFG());
    return false;
}

namespace {
struct PDGPassGraphTraits {
    static tsar::PDG *getGraph(PDGPass *P) { return &P->getPDG(); }
};

struct PDGPrinter : public DOTGraphTraitsPrinterWrapperPass<
    PDGPass, false, tsar::PDG*,
    PDGPassGraphTraits> {
    static char ID;
    PDGPrinter() : DOTGraphTraitsPrinterWrapperPass<PDGPass, false,
        tsar::PDG*, PDGPassGraphTraits>("pdg", ID) {
        initializePDGPrinterPass(*PassRegistry::getPassRegistry());
    }
};
char PDGPrinter::ID = 0;

struct PDGViewer : public DOTGraphTraitsViewerWrapperPass<
    PDGPass, false, tsar::PDG*,
    PDGPassGraphTraits> {
    static char ID;
    PDGViewer() : DOTGraphTraitsViewerWrapperPass<PDGPass, false,
        tsar::PDG*, PDGPassGraphTraits>("pdg", ID) {
        initializePDGViewerPass(*PassRegistry::getPassRegistry());
    }
};
char PDGViewer::ID = 0;
} //anonymous namespace

INITIALIZE_PASS_IN_GROUP(PDGViewer, "view-pdg",
    "View Program Dependency Graph", true, true,
    DefaultQueryManager::OutputPassGroup::getPassRegistry())

INITIALIZE_PASS_IN_GROUP(PDGPrinter, "print-pdg",
    "Print Program Dependency Graph", true, true,
    DefaultQueryManager::OutputPassGroup::getPassRegistry())

FunctionPass *llvm::createPDGPrinter() {
  return new PDGPrinter;
}

FunctionPass *llvm::createPDGViewer() {
  return new PDGViewer;
}