#ifndef TSAR_INCLUDE_BUILDPDG_H
#define TSAR_INCLUDE_BUILDPDG_H

#include "tsar/Analysis/Passes.h"
#include "tsar/Analysis/Clang/SourceCFG.h"
#include <bcl/utility.h>
#include <llvm/ADT/DirectedGraph.h>
#include <llvm/Support/GenericDomTree.h>
#include <llvm/ADT/DepthFirstIterator.h>
#include <llvm/ADT/GraphTraits.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/Support/DOTGraphTraits.h>
#include <llvm/Pass.h>
#include <map>
//
#include <llvm/IR/CFG.h>
//

namespace tsar {

template<typename CFGType>
struct CFGTrairs {

};

template<typename CFGType>
class CDGNode;
template<typename CFGType>
class CDGEdge;
template<typename CFGType>
class CDGBuilder;

template<typename CFGType>
using CDGNodeBase=llvm::DGNode<CDGNode<CFGType>, CDGEdge<CFGType>>;
template<typename CFGType>
using CDGEdgeBase=llvm::DGEdge<CDGNode<CFGType>, CDGEdge<CFGType>>;
template<typename CFGType>
using CDGBase=llvm::DirectedGraph<CDGNode<CFGType>, CDGEdge<CFGType>>;

template<typename CFGType>
class CDGEdge : public CDGEdgeBase<CFGType> {
public:
	//CDGEdge(CDGNode &_TargetNode) : CDGEdgeBase(_TargetNode) {}
};

template<typename CFGType>
class CDGNode : public CDGNodeBase<CFGType> {
public:
	using CFGNodeType=GraphTraits<CFGType>::NodeRef;

	enum class NodeKind {Entry, Default, Region};
	CDGNode(CFGNodeType _mBlock)
			: mBlock(_mBlock), mKind(NodeKind::Default) {}
	inline NodeKind getKind() const { return mKind; }
	inline CFGNodeType getBlock() const { return mBlock; }
private:
	CFGNodeType mBlock;
	NodeKind mKind;
};

template<typename CFGType>
class ControlDependenceGraph : public CDGBase<CFGType> {
	friend class PDGBuilder;
public:
	using NodeType=GraphTraits<CFGType>::NodeRef;
	using CFGNodeMapType=llvm::DenseMap<CFGNodeType, CDGNode>;

	struct AugmentedCFG {
		CFGType *CFG;
	}

	PDG(const std::string &_FunctionName, CFGType *_mCFG)
			: FunctionName(_FunctionName), mSCFG(_mCFG) {}

	inline bool addNode(CDGNode &N) {
		if (CDGBase::addNode(N)) {
			BlockToNodeMap.insert({N.getBlock(), &N});
			return true;
		}
		else
			return false;
	}

	CDGNode &emplaceNode(SourceCFGNode *Block) {
		CDGNode *NewNode=new CDGNode(Block);
		addNode(*NewNode);
		return *NewNode;
	}

	inline void bindNodes(CDGNode &SourceNode, CDGNode &TargetNode,
			CDGEdge::EdgeKind _Ekind) {
		connect(SourceNode, TargetNode, *(new CDGEdge(TargetNode, _Ekind)));
	}

	inline CDGNode *getNode(SourceCFGNode *Block) {
		return BlockToNodeMap[Block];
	}

	inline CDGNode *getEntryNode() {
		return getNode(mSCFG->getEntryNode());
	}

	~PDG() {
		for (auto N : Nodes) {
			for (auto E : N->getEdges())
				delete E;
			delete N;
		}
	}
private:
	std::string FunctionName;
	CFGNodeMapType BlockToNodeMap;
	CFGType *mCFG;
};

template<typename CFGType>
class CDGBuilder {
public:
	CDGBuilder() : mPDG(nullptr) {}
	ControlDependenceGraph<CFGType> *populate(CFGType &_SCFG);
private:
	inline void processControlDependence();
	inline llvm::DomTreeNodeBase<SourceCFGNode> *getRealRoot() {
		return *mSPDT.getRootNode()->begin();
	}
	ControlDependeceGraph<CFGType> *mCDG;
	CFGType *mCFG;
	llvm::PostDomTreeBase<SourceCFGNode> mSPDT;
};

}//namespace tsar

namespace llvm {

template<typename CFGType>
class CDGPass : public FunctionPass, private bcl::Uncopyable {
public:
	static char ID;
	CDGPass() : FunctionPass(ID), mCDG(nullptr) {
		initializePDGPassPass(*PassRegistry::getPassRegistry());
	}
	bool runOnFunction(Function &F) override;
	void getAnalysisUsage(AnalysisUsage &AU) const override;
	void releaseMemory() {
		mCDGBuilder=tsar::CDGBuilder<CFGType>();
		if (mCDG) {
			delete mCDG;
			mCDG=nullptr;
		}
	}
	inline tsar::ControlDependenceGraph<CFGType> &getCDG() { return *mCDG; }
private:
	tsar::CDGBuilder<CFGType> mCDGBuilder;
	tsar::ControlDependenceGraph<CFGType> *mCDG;
};

using SourceCDGPass=CDGPass<tsar::SourceCFG>;
using IRCDGPass=CDGPass<Function>;

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

template<bool IsPostDom> struct GraphTraits<DominatorTreeBase<
	tsar::SourceCFGNode, IsPostDom>*>
			: public GraphTraits<DomTreeNodeBase<tsar::SourceCFGNode>*> {
	static NodeRef getEntryNode(DominatorTreeBase<tsar::SourceCFGNode,
			IsPostDom> *Tree) {
		return Tree->getRootNode();
	}
	using nodes_iterator=df_iterator<DomTreeNodeBase<tsar::SourceCFGNode>*>;
	static nodes_iterator nodes_begin(DominatorTreeBase<tsar::SourceCFGNode,
			IsPostDom> *Tree) {
		return df_begin(Tree->getRootNode());
	}
	static nodes_iterator nodes_end(DominatorTreeBase<tsar::SourceCFGNode,
			IsPostDom> *Tree) {
		return df_end(Tree->getRootNode());
	}
	static unsigned size(DominatorTreeBase<tsar::SourceCFGNode,
			IsPostDom> *Tree) {
		return Tree->root_size();
	}
};

template<bool IsPostDom> struct DOTGraphTraits<DominatorTreeBase<
		tsar::SourceCFGNode, IsPostDom>*> : public DefaultDOTGraphTraits {
	DOTGraphTraits(bool IsSimple=false) : DefaultDOTGraphTraits(IsSimple) {}
	static std::string getGraphName(const DominatorTreeBase<tsar::SourceCFGNode,
			IsPostDom> *Tree) {
		return IsPostDom?"Post-Dominator Tree":"Dominator Tree";
	}
	std::string getNodeLabel(DomTreeNodeBase<tsar::SourceCFGNode> *Node,
			DominatorTreeBase<tsar::SourceCFGNode, IsPostDom> *Tree) {
		return (std::string)*Node->getBlock();
	}
	static bool isNodeHidden(DomTreeNodeBase<tsar::SourceCFGNode> *Node,
			DominatorTreeBase<tsar::SourceCFGNode, IsPostDom> *Tree) {
		Tree->isVirtualRoot(Node)?true:false;
	}
};

template<> struct GraphTraits<tsar::CDGNode*> {
	using NodeRef=tsar::CDGNode*;
	static tsar::CDGNode *PDGGetTargetNode(tsar::CDGEdge *E) {
		return &E->getTargetNode();
	}
	using ChildIteratorType=mapped_iterator<tsar::CDGNode::iterator,
			decltype(&PDGGetTargetNode)>;
	using ChildEdgeIteratorType=tsar::CDGNode::iterator;
	static NodeRef getEntryNode(NodeRef N) { return N; }
	static ChildIteratorType child_begin(NodeRef N) {
		return ChildIteratorType(N->begin(), &PDGGetTargetNode);
	}
	static ChildIteratorType child_end(NodeRef N) {
		return ChildIteratorType(N->end(), &PDGGetTargetNode);
	}
	static ChildEdgeIteratorType child_edge_begin(NodeRef N) {
		return N->begin();
	}
	static ChildEdgeIteratorType child_edge_end(NodeRef N) { return N->end(); }
};

template<> struct GraphTraits<tsar::PDG*> :
		public GraphTraits<tsar::CDGNode*> {
	using nodes_iterator=tsar::PDG::iterator;
	static NodeRef getEntryNode(tsar::PDG *Graph) {
		return Graph->getEntryNode();
	}
	static nodes_iterator nodes_begin(tsar::PDG *Graph) {
		return Graph->begin();
	}
	static nodes_iterator nodes_end(tsar::PDG *Graph) {
		return Graph->end();
	}
	using EdgeRef=tsar::CDGEdge*;
	static NodeRef edge_dest(EdgeRef E) { return &E->getTargetNode(); }
	static unsigned size(tsar::PDG *Graph) { return Graph->size(); }
};

template<> struct DOTGraphTraits<tsar::PDG*> : public DefaultDOTGraphTraits {
	DOTGraphTraits(bool IsSimple=false) : DefaultDOTGraphTraits(IsSimple) {}
	static std::string getGraphName(const tsar::PDG *Graph) {
		return "Control Dependence Graph";
	}
	std::string getNodeLabel(const tsar::CDGNode *Node, const tsar::PDG *Graph) {
		return (std::string)*Node->getBlock();
	}
};

}//namespace llvm

#endif//TSAR_INCLUDE_BUILDPDG_H