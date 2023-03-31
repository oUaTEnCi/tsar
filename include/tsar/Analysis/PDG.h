#ifndef TSAR_INCLUDE_BUILDPDG_H
#define TSAR_INCLUDE_BUILDPDG_H

#include <llvm/Pass.h>
#include <bcl/utility.h>
#include "tsar/Analysis/Passes.h"
#include "tsar/Analysis/Clang/SourceCFG.h"

//#include <llvm/ADT/DirectedGraph.h>
//#include <llvm/ADT/DenseMap.h>
//#include <llvm/ADT/SmallVector.h>
//#include <llvm/ADT/SmallPtrSet.h>
//#include <llvm/ADT/GraphTraits.h>
#include <llvm/Support/DOTGraphTraits.h>
#include <llvm/Pass.h>
#include <bcl/utility.h>

#include <llvm/Support/GenericDomTree.h>
#include <llvm/Support/GenericDomTreeConstruction.h>

namespace tsar {

class PDGNode;
class PDGEdge;
class PDG;
class PDGBuilder;
using PDGNodeBase=llvm::DGNode<PDGNode, PDGEdge>;
using PDGEdgeBase=llvm::DGEdge<PDGNode, PDGEdge>;
using PDGBase=llvm::DirectedGraph<PDGNode, PDGEdge>;

class PDGEdge : public PDGEdgeBase {
public:
    enum class EdgeKind {ControlDependence, DataDependence};
    PDGEdge(PDGNode &_TargetNode, EdgeKind _Kind) : PDGEdgeBase(_TargetNode), Kind(_Kind) {}
    inline EdgeKind getKind() const { return Kind; }
    explicit operator std::string() const {
        switch (Kind) {
            case EdgeKind::ControlDependence:
            case EdgeKind::DataDependence:
                return "";
        }
    }
private:
    EdgeKind Kind;
};

class PDGNode : public PDGNodeBase {
public:
    enum class NodeKind {Default, Region};
    PDGNode(SourceBasicBlock *_SBB, const NodeKind _Kind) : SBB(_SBB), Kind(_Kind) {}
    PDGNode(const PDGNode &_Node) : PDGNodeBase(_Node), SBB(_Node.SBB), Kind(_Node.Kind) {}
    inline NodeKind getKind() const { return Kind; }
    explicit operator std::string() const {
        switch (Kind) {
            case NodeKind::Default:
                return "START"+(std::string)*SBB;
            case NodeKind::Region:
                return "REGION";
        }
    }
private:
    SourceBasicBlock *SBB;
    NodeKind Kind;
};

class PDG : public PDGBase {
    friend class PDGBuilder;
public:
    PDG(const std::string &_FunctionName) : FunctionName(_FunctionName), EntryNode(nullptr) {}
    ~PDG() {
        for (auto N : Nodes) {
            for (auto E : N->getEdges())
                delete E;
            delete N;
        }
    }
    inline PDGNode *getEntryNode() { return EntryNode; }
    inline void bindNodes(PDGNode &SourceNode, PDGNode &TargetNode, PDGEdge::EdgeKind _Ekind) {
        connect(SourceNode, TargetNode, *(new PDGEdge(TargetNode, _Ekind)));
    }
private:
    llvm::PostDomTreeBase<SourceCFGNode> *SCFGPD;
    std::string FunctionName;
    PDGNode *EntryNode;
};

class PDGBuilder {
public:
    PDGBuilder() : mPDG(nullptr) {}
    PDG *populate(const SourceCFG &_SCFG);
private:
    PDG *mPDG;
};

}//namespace tsar

namespace llvm {

class PDGPass : public FunctionPass, private bcl::Uncopyable {
public:
    static char ID;
    PDGPass() : FunctionPass(ID), mPDG(nullptr) {
        initializePDGPassPass(*PassRegistry::getPassRegistry());
    }
    bool runOnFunction(Function &F) override;
    void getAnalysisUsage(AnalysisUsage &AU) const override;
    void releaseMemory() {
        mPDGBuilder=tsar::PDGBuilder();
        if (mPDG) {
            //delete mPDG;
            mPDG=nullptr;
        }
    }
    inline tsar::PDG &getPDG() { return *mPDG; }
private:
    tsar::PDGBuilder mPDGBuilder;
    tsar::PDG *mPDG;
};

template<> struct GraphTraits<tsar::PDGNode*> {
    using NodeRef=tsar::PDGNode*;
    static tsar::PDGNode *PDGGetTargetNode(tsar::PDGEdge *E) { return &E->getTargetNode(); }
    using ChildIteratorType=mapped_iterator<tsar::PDGNode::iterator, decltype(&PDGGetTargetNode)>;
    using ChildEdgeIteratorType=tsar::PDGNode::iterator;
    static NodeRef getEntryNode(NodeRef N) { return N; }
    static ChildIteratorType child_begin(NodeRef N) { return ChildIteratorType(N->begin(), &PDGGetTargetNode); }
    static ChildIteratorType child_end(NodeRef N) { return ChildIteratorType(N->end(), &PDGGetTargetNode); }
    static ChildEdgeIteratorType child_edge_begin(NodeRef N) { return N->begin(); }
    static ChildEdgeIteratorType child_edge_end(NodeRef N) { return N->end(); }
};

template<> struct GraphTraits<tsar::PDG*> : public GraphTraits<tsar::PDGNode*> {
    using nodes_iterator=tsar::PDG::iterator;
    static NodeRef getEntryNode(tsar::PDG *PDG) { return PDG->getEntryNode(); }
    static nodes_iterator nodes_begin(tsar::PDG *PDG) { return PDG->begin(); }
    static nodes_iterator nodes_end(tsar::PDG *PDG) { return PDG->end(); }
    using EdgeRef=tsar::PDGEdge*;
    static NodeRef edge_dest(EdgeRef E) { return &E->getTargetNode(); }
    static unsigned size(tsar::PDG *PDG) { return PDG->size(); }
};

template<> struct DOTGraphTraits<tsar::PDG*> : public DefaultDOTGraphTraits {
    DOTGraphTraits(bool IsSimple=false) : DefaultDOTGraphTraits(IsSimple) {}
    static std::string getGraphName(const tsar::PDG *PDG) { return "PDG"; }
    std::string getNodeLabel(tsar::PDGNode *Node, tsar::PDG *PDG) { return (std::string)*Node; }
    std::string getEdgeSourceLabel(tsar::PDGNode *Node, GraphTraits<tsar::PDG*>::ChildIteratorType It) { return (std::string)**It.getCurrent(); }
};

}//namespace llvm

#endif//TSAR_INCLUDE_BUILDPDG_H