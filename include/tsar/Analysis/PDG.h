#ifndef TSAR_INCLUDE_BUILDPDG_H
#define TSAR_INCLUDE_BUILDPDG_H

#include <llvm/Pass.h>
#include <bcl/utility.h>
#include "tsar/Analysis/Passes.h"
#include "tsar/Analysis/Clang/SourceCFG.h"
#include <llvm/Support/DOTGraphTraits.h>
#include <llvm/Pass.h>
#include <bcl/utility.h>

#include <llvm/Support/GenericDomTree.h>
#include <llvm/Support/GenericDomTreeConstruction.h>

//
#include <llvm/ADT/DepthFirstIterator.h>
//

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
    //
    inline llvm::DomTreeBase<SourceCFGNode> *getDomTree() { return SCFGPD; }
    //
private:
    //llvm::PostDomTreeBase<SourceCFGNode> *SCFGPD;
    llvm::DomTreeBase<SourceCFGNode> *SCFGPD;
    std::string FunctionName;
    PDGNode *EntryNode;
};

class PDGBuilder {
public:
    PDGBuilder() : mPDG(nullptr) {}
    //PDG *populate(const SourceCFG &_SCFG);
    PDG *populate(SourceCFG &_SCFG);
private:
    PDG *mPDG;
    //
    SourceCFG *mSCFG;
    //
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
            //mPDG=nullptr;
            ;
        }
    }
    //inline tsar::PDG &getPDG() { return *mPDG; }
    inline DomTreeBase<tsar::SourceCFGNode> &getDomTree() { return *mPDG->getDomTree(); }
private:
    tsar::PDGBuilder mPDGBuilder;
    tsar::PDG *mPDG;
};

/*
template<> struct GraphTraits<tsar::SourceCFGNode*> {
	using NodeRef=tsar::SourceCFGNode*;
	static tsar::SourceCFGNode *SCFGGetTargetNode(tsar::SourceCFGEdge *E) {
		return &E->getTargetNode();
	}
	using ChildIteratorType=mapped_iterator<tsar::SourceCFGNode::iterator, decltype(&SCFGGetTargetNode)>;
	using ChildEdgeIteratorType=tsar::SourceCFGNode::iterator;
	static NodeRef getEntryNode(NodeRef N) { return N; }
	static ChildIteratorType child_begin(NodeRef N) {
		return ChildIteratorType(N->begin(), &SCFGGetTargetNode);
	}
	static ChildIteratorType child_end(NodeRef N) {
		return ChildIteratorType(N->end(), &SCFGGetTargetNode);
	}
	static ChildEdgeIteratorType child_edge_begin(NodeRef N) {
		return N->begin();
	}
	static ChildEdgeIteratorType child_edge_end(NodeRef N) { return N->end(); }
};

template<> struct GraphTraits<tsar::SourceCFG*> : public GraphTraits<tsar::SourceCFGNode*> {
	using nodes_iterator=tsar::SourceCFG::iterator;
	static NodeRef getEntryNode(tsar::SourceCFG *mSCFG) {
		return mSCFG->getStartNode();
	}
	static nodes_iterator nodes_begin(tsar::SourceCFG *mSCFG) {
		return mSCFG->begin();
	}
	static nodes_iterator nodes_end(tsar::SourceCFG *mSCFG) {
		return mSCFG->end();
	}
	using EdgeRef=tsar::SourceCFGEdge*;
	static NodeRef edge_dest(EdgeRef E) { return &E->getTargetNode(); }
	static unsigned size(tsar::SourceCFG *mSCFG) { return mSCFG->size(); }
};
*/

//Не мусор
template<> struct GraphTraits<DomTreeNodeBase<tsar::SourceCFGNode>*> {
    using NodeRef=DomTreeNodeBase<tsar::SourceCFGNode>*;
    using ChildIteratorType=DomTreeNodeBase<tsar::SourceCFGNode>::iterator;
    static NodeRef getEntryNode(NodeRef N) { return N; }
    static ChildIteratorType child_begin(NodeRef N) {
        return N->begin();
    }
    static ChildIteratorType child_end(NodeRef N) {
        return N->end();
    }
};

template<> struct GraphTraits<DomTreeBase<tsar::SourceCFGNode>*> : public GraphTraits<DomTreeNodeBase<tsar::SourceCFGNode>*> {
    static NodeRef getEntryNode(DomTreeBase<tsar::SourceCFGNode> *Tree) { return Tree->getRootNode(); }

    using nodes_iterator=df_iterator<DomTreeBase<tsar::SourceCFGNode>*>;
    static nodes_iterator nodes_begin(DomTreeBase<tsar::SourceCFGNode> *Tree) { return df_begin(Tree); }
    static nodes_iterator nodes_end(DomTreeBase<tsar::SourceCFGNode> *Tree) { return df_end(Tree); }
    static unsigned size(DomTreeBase<tsar::SourceCFGNode> *Tree) { return Tree->root_size(); }
};

template<> struct DOTGraphTraits<DomTreeBase<tsar::SourceCFGNode>*> : public DefaultDOTGraphTraits {
    DOTGraphTraits(bool IsSimple=false) : DefaultDOTGraphTraits(IsSimple) {}
    static std::string getGraphName(const DomTreeBase<tsar::SourceCFGNode> *Tree) { return "Dom_Tree"; }
    std::string getNodeLabel(DomTreeNodeBase<tsar::SourceCFGNode> *Node,
            DomTreeBase<tsar::SourceCFGNode> *Tree) {
        return (std::string)*Node->getBlock();
    }
};
//



}//namespace llvm

#endif//TSAR_INCLUDE_BUILDPDG_H