#ifndef TSAR_INCLUDE_BUILDPDG_H
#define TSAR_INCLUDE_BUILDPDG_H

#include "tsar/Analysis/Passes.h"
#include "tsar/Analysis/Clang/SourceCFG.h"
#include <bcl/utility.h>
#include <llvm/IR/CFG.h>
#include <llvm/ADT/DirectedGraph.h>
#include <llvm/Support/GenericDomTree.h>
#include <llvm/ADT/DepthFirstIterator.h>
#include <llvm/ADT/GraphTraits.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/Support/DOTGraphTraits.h>
#include <llvm/Support/Caching.h>
#include <llvm/Pass.h>
#include <map>
#include <string>

#include <llvm/Analysis/DDG.h>
#include <llvm/Analysis/DDGPrinter.h>

namespace tsar {

template<typename CFGType, typename CFGNodeType>
class CDGNode;
template<typename CFGType, typename CFGNodeType>
class CDGEdge;
template<typename CDGType>
class CDGBuilder;

template<typename CFGType, typename CFGNodeType>
using CDGNodeBase=llvm::DGNode<CDGNode<CFGType, CFGNodeType>, CDGEdge<CFGType, CFGNodeType>>;
template<typename CFGType, typename CFGNodeType>
using CDGEdgeBase=llvm::DGEdge<CDGNode<CFGType, CFGNodeType>, CDGEdge<CFGType, CFGNodeType>>;
template<typename CFGType, typename CFGNodeType>
using CDGBase=llvm::DirectedGraph<CDGNode<CFGType, CFGNodeType>, CDGEdge<CFGType, CFGNodeType>>;

template<typename CFGType, typename CFGNodeType>
class CDGEdge : public CDGEdgeBase<CFGType, CFGNodeType> {
public:
	using NodeType=CDGNode<CFGType, CFGNodeType>;
	CDGEdge(NodeType &_TargetNode) : CDGEdgeBase<CFGType, CFGNodeType>(_TargetNode){}
};

template<typename CFGType, typename CFGNodeType>
class CDGNode : public CDGNodeBase<CFGType, CFGNodeType> {
public:
	enum class NodeKind {Entry, Default, Region};
	CDGNode(NodeKind _mKind) : mKind(_mKind) {}
	inline NodeKind getKind() const { return mKind; }
	void destroy();
private:
	NodeKind mKind;
};

template<typename CFGType, typename CFGNodeType>
class EntryCDGNode : public CDGNode<CFGType, CFGNodeType> {
private:
	using Base=CDGNode<CFGType, CFGNodeType>;
public:
	EntryCDGNode() : Base(Base::NodeKind::Entry) {}
	static bool classof(const Base *Node) { return Node->getKind()==Base::NodeKind::Entry; }
};

template<typename CFGType, typename CFGNodeType>
class DefaultCDGNode : public CDGNode<CFGType, CFGNodeType> {
private:
	using Base=CDGNode<CFGType, CFGNodeType>;
public:
	using NodeValueType=CFGNodeType*;
	DefaultCDGNode(NodeValueType _mBlock) : Base(Base::NodeKind::Default), mBlock(_mBlock) {}
	static bool classof(const Base *Node) { return Node->getKind()==Base::NodeKind::Default; }
	inline NodeValueType getBlock() const { return mBlock; }
private:
	NodeValueType mBlock;
};

template<typename CFGType, typename CFGNodeType>
class ControlDependenceGraph : public CDGBase<CFGType, CFGNodeType> {
private:
	using DefaultNode=DefaultCDGNode<CFGType, CFGNodeType>;
	using EntryNode=EntryCDGNode<CFGType, CFGNodeType>;
public:
	using CFGT=CFGType;
	using CFGNodeT=CFGNodeType;
	using NodeType=CDGNode<CFGType, CFGNodeType>;
	using EdgeType=CDGEdge<CFGType, CFGNodeType>;
	// TODO: NodeValueType must be declared once for all DirectedGraph classes
	using NodeValueType=CFGNodeType*;
	using CFGNodeMapType=llvm::DenseMap<NodeValueType, NodeType*>;

	ControlDependenceGraph(const std::string &_FunctionName, CFGType *_mCFG)
			: FunctionName(_FunctionName), mCFG(_mCFG), mEntryNode(new EntryNode()) {
		CDGBase<CFGType, CFGNodeType>::addNode(*mEntryNode);
	}

	inline bool addNode(DefaultNode &N) {
		if (CDGBase<CFGType, CFGNodeType>::addNode(N)) {
			BlockToNodeMap.insert({N.getBlock(), &N});
			return true;
		}
		else
			return false;
	}

	NodeType &emplaceNode(NodeValueType Block) {
		DefaultNode *NewNode=new DefaultNode(Block);
		addNode(*NewNode);
		return *NewNode;
	}

	inline void bindNodes(NodeType &SourceNode, NodeType &TargetNode) {
		CDGBase<CFGType, CFGNodeType>::connect(SourceNode, TargetNode, *(new CDGEdge(TargetNode)));
	}

	inline NodeType *getNode(NodeValueType Block) {
		return BlockToNodeMap[Block];
	}

	inline NodeType *getEntryNode() {
		return mEntryNode;
	}

	inline CFGType *getCFG() const {
		return mCFG;
	}

	~ControlDependenceGraph() {
		for (auto N : CDGBase<CFGType, CFGNodeType>::Nodes) {
			for (auto E : N->getEdges())
				delete E;
			delete N;
		}
	}
private:
	EntryNode *mEntryNode;
	std::string FunctionName;
	CFGNodeMapType BlockToNodeMap;
	CFGType *mCFG;
};

using SourceCDG=ControlDependenceGraph<SourceCFG, SourceCFGNode>;
using IRCDGNode=CDGNode<llvm::Function, llvm::BasicBlock>;
using DefaultIRCDGNode=DefaultCDGNode<llvm::Function, llvm::BasicBlock>;
using EntryIRCDGNode=EntryCDGNode<llvm::Function, llvm::BasicBlock>;
using IRCDGEdge=CDGEdge<llvm::Function, llvm::BasicBlock>;
using IRCDG=ControlDependenceGraph<llvm::Function, llvm::BasicBlock>;

}//namespace tsar

namespace llvm {

template<typename CFGType, typename CFGNodeType>
struct GraphTraits<tsar::CDGNode<CFGType, CFGNodeType>*> {
	using NodeRef=tsar::CDGNode<CFGType, CFGNodeType>*;
	static tsar::CDGNode<CFGType, CFGNodeType> *CDGGetTargetNode(tsar::CDGEdge<CFGType, CFGNodeType> *E) {
		return &E->getTargetNode();
	}
	using ChildIteratorType=mapped_iterator<typename tsar::CDGNode<CFGType, CFGNodeType>::iterator,
			decltype(&CDGGetTargetNode)>;
	using ChildEdgeIteratorType=typename tsar::CDGNode<CFGType, CFGNodeType>::iterator;
	static NodeRef getEntryNode(NodeRef N) { return N; }
	static ChildIteratorType child_begin(NodeRef N) {
		return ChildIteratorType(N->begin(), &CDGGetTargetNode);
	}
	static ChildIteratorType child_end(NodeRef N) {
		return ChildIteratorType(N->end(), &CDGGetTargetNode);
	}
	static ChildEdgeIteratorType child_edge_begin(NodeRef N) {
		return N->begin();
	}
	static ChildEdgeIteratorType child_edge_end(NodeRef N) { return N->end(); }
};

template<typename CFGType, typename CFGNodeType>
struct GraphTraits<tsar::ControlDependenceGraph<CFGType, CFGNodeType>*> :
		public GraphTraits<tsar::CDGNode<CFGType, CFGNodeType>*> {
	using nodes_iterator=typename tsar::ControlDependenceGraph<CFGType, CFGNodeType>::iterator;
	static typename llvm::GraphTraits<tsar::CDGNode<CFGType, CFGNodeType>*>::NodeRef getEntryNode(tsar::ControlDependenceGraph<CFGType, CFGNodeType> *Graph) {
		return Graph->getEntryNode();
	}
	static nodes_iterator nodes_begin(tsar::ControlDependenceGraph<CFGType, CFGNodeType> *Graph) {
		return Graph->begin();
	}
	static nodes_iterator nodes_end(tsar::ControlDependenceGraph<CFGType, CFGNodeType> *Graph) {
		return Graph->end();
	}
	using EdgeRef=tsar::CDGEdge<CFGType, CFGNodeType>*;
	static typename llvm::GraphTraits<tsar::CDGNode<CFGType, CFGNodeType>*>::NodeRef edge_dest(EdgeRef E) { return &E->getTargetNode(); }
	static unsigned size(tsar::ControlDependenceGraph<CFGType, CFGNodeType> *Graph) { return Graph->size(); }
};

template<typename CFGType, typename CFGNodeType>
struct DOTGraphTraits<tsar::ControlDependenceGraph<CFGType, CFGNodeType>*> :
		public DOTGraphTraits<CFGType*> {
private:
	using GTInstanced=GraphTraits<tsar::ControlDependenceGraph<CFGType, CFGNodeType>*>;
public:
	DOTGraphTraits(bool IsSimple=false) : DOTGraphTraits<CFGType*>(IsSimple) {}
	static std::string getGraphName(const tsar::ControlDependenceGraph<CFGType, CFGNodeType> *Graph) {
		return "Control Dependence Graph";
	}
	std::string getNodeLabel(const tsar::CDGNode<CFGType, CFGNodeType> *Node,
			const tsar::ControlDependenceGraph<CFGType, CFGNodeType> *Graph) {
		if (auto *DefNode=dyn_cast<tsar::DefaultCDGNode<CFGType, CFGNodeType>>(Node))
			return DOTGraphTraits<CFGType*>::getNodeLabel(DefNode->getBlock(), Graph->getCFG());
		else
			return "Entry";
	}
	std::string getNodeAttributes(const tsar::CDGNode<CFGType, CFGNodeType> *Node,
			tsar::ControlDependenceGraph<CFGType, CFGNodeType> *Graph) {
		if (auto *DefNode=dyn_cast<tsar::DefaultCDGNode<CFGType, CFGNodeType>>(Node))
			return DOTGraphTraits<CFGType*>::getNodeAttributes(DefNode->getBlock(), Graph->getCFG());
		else
			return "";
	}
	std::string getEdgeSourceLabel(const tsar::CDGNode<CFGType, CFGNodeType> *Node,
			typename GraphTraits<tsar::ControlDependenceGraph<CFGType, CFGNodeType>*>::ChildIteratorType It) { return ""; }
	std::string getEdgeAttributes(const tsar::CDGNode<CFGType, CFGNodeType> *Node,
			typename GTInstanced::ChildIteratorType EdgeIt,
			tsar::ControlDependenceGraph<CFGType, CFGNodeType> *Graph) {
		//return DOTGraphTraits<CFGType*>::getEdgeAttributes(Node->getBlock(), EdgeIt, Graph->getCFG());
		return "";
	}
	bool isNodeHidden(const tsar::CDGNode<CFGType, CFGNodeType> *Node,
			tsar::ControlDependenceGraph<CFGType, CFGNodeType>*) {
		return false;
	}
};

}//namespace llvm

namespace tsar {
template<typename CDGType>
class CDGBuilder {
private:
	using CFGType=typename CDGType::CFGT;
	using CFGNodeType=typename CDGType::CFGNodeT;
public:
	// TODO: NodeValueType must be declared once for all DirectedGraph classes
	using NodeValueType=typename llvm::GraphTraits<CFGType*>::NodeRef;

	CDGBuilder() : mCDG(nullptr) {}
	CDGType *populate(CFGType &_CFG);
private:
	inline void processControlDependence();
	CFGType *mCFG;
	CDGType *mCDG;
	llvm::PostDomTreeBase<CFGNodeType> mPDT;
};
}; //namespace tsar

namespace llvm {

template<typename CDGType>
class CDGPass : public FunctionPass, private bcl::Uncopyable {
private:
	using CFGType=typename CDGType::CFGT;
	using CFGNodeType=typename CDGType::CFGNodeT;
public:
	static char ID;
	CDGPass();
	bool runOnFunction(Function &F) override;
	void getAnalysisUsage(AnalysisUsage &AU) const override;
	void releaseMemory() {
		mCDGBuilder=tsar::CDGBuilder<CDGType>();
		if (mCDG) {
			delete mCDG;
			mCDG=nullptr;
		}
	}
	inline tsar::ControlDependenceGraph<CFGType, CFGNodeType> &getCDG() { return *mCDG; }
private:
	tsar::CDGBuilder<CDGType> mCDGBuilder;
	tsar::ControlDependenceGraph<CFGType, CFGNodeType> *mCDG;
};

template<>
CDGPass<tsar::ControlDependenceGraph<tsar::SourceCFG, tsar::SourceCFGNode>>::CDGPass() : FunctionPass(ID), mCDG(nullptr) {
	initializeSourceCDGPassPass(*PassRegistry::getPassRegistry());
}

template<>
CDGPass<tsar::ControlDependenceGraph<Function, BasicBlock>>::CDGPass() : FunctionPass(ID), mCDG(nullptr) {
	initializeIRCDGPassPass(*PassRegistry::getPassRegistry());
}

using SourceCDGPass=CDGPass<tsar::ControlDependenceGraph<tsar::SourceCFG, tsar::SourceCFGNode>>;
using IRCDGPass=CDGPass<tsar::ControlDependenceGraph<Function, BasicBlock>>;

};//namespace llvm

namespace tsar {
class ControlPDGEdge : public llvm::DDGEdge {
public:
	ControlPDGEdge(llvm::DDGNode &TargetNode) : llvm::DDGEdge(TargetNode, EdgeKind::Unknown) {}
};

class ProgramDependencyGraph  : public llvm::DataDependenceGraph {
private:
	using Base=llvm::DataDependenceGraph;
	void construct(llvm::Function &F) {
		//
		for (auto INode : *this)
			for (auto Edge : INode->getEdges())
				assert(Edge->getKind()!=llvm::DDGEdge::EdgeKind::Unknown);
		//
		IRCDG *CDG=CDGBuilder<IRCDG>().populate(F);
		std::map<llvm::Instruction*, llvm::DDGNode*> InstrMap;
		for (auto INode : *this)
			if (llvm::isa<llvm::SimpleDDGNode>(INode))
				for (auto I : llvm::dyn_cast<llvm::SimpleDDGNode>(INode)->getInstructions())
					InstrMap[I]=INode;
		for (auto It : InstrMap)
			if (It.first->isTerminator()) {
				IRCDGNode *CDGNode=CDG->getNode(It.first->getParent());
				for (auto CDGEdge : *CDGNode) {
					//llvm::BasicBlock *TargetBB=(llvm::dyn_cast<DefaultIRCDGNode>(CDGEdge->getTargetNode())).getBlock();
					llvm::BasicBlock *TargetBB=((DefaultIRCDGNode*)(&CDGEdge->getTargetNode()))->getBlock();
					for (auto IIt=TargetBB->begin(); IIt!=TargetBB->end(); ++IIt)
						connect(*InstrMap[It.first], *InstrMap[&(*IIt)], *(new ControlPDGEdge(*InstrMap[&(*IIt)])));
				}
			}
		for (auto RootEdge : getRoot().getEdges())
			delete RootEdge;
		getRoot().clear();
		for (auto CDGEdge : *(CDG->getEntryNode()))
			//for (auto IIt=llvm::dyn_cast<DefaultIRCDGNode>(CDGEdge->getTargetNode()).getBlock()->begin();
			for (auto IIt=((DefaultIRCDGNode*)(&CDGEdge->getTargetNode()))->getBlock()->begin();
				//IIt!=llvm::dyn_cast<DefaultIRCDGNode>(&CDGEdge->getTargetNode())->getBlock()->end(); ++IIt)
				IIt!=((DefaultIRCDGNode*)(&CDGEdge->getTargetNode()))->getBlock()->end(); ++IIt)
				connect(getRoot(), *InstrMap[&(*IIt)], *(new ControlPDGEdge(*InstrMap[&(*IIt)])));
		delete CDG;
	}
public:
	ProgramDependencyGraph(llvm::Function &F, llvm::DependenceInfo &DI)
			: Base(F, DI) {
		construct(F);
	}
};
}

namespace llvm {
template <>
struct GraphTraits<tsar::ProgramDependencyGraph *> : public GraphTraits<DataDependenceGraph*> {};

template <>
struct GraphTraits<const tsar::ProgramDependencyGraph *> : public GraphTraits<const DataDependenceGraph*> {};

template <>
struct DOTGraphTraits<const tsar::ProgramDependencyGraph *> : public DOTGraphTraits<const DataDependenceGraph*> {
private:
	using GT=GraphTraits<const tsar::ProgramDependencyGraph *>;
	using BaseDOTGT=DOTGraphTraits<const DataDependenceGraph*>;
public:
	DOTGraphTraits(bool isSimple=false) : BaseDOTGT(isSimple) {}

	std::string getNodeLabel(const DDGNode *Node, const tsar::ProgramDependencyGraph *PDG) {
		return BaseDOTGT::getNodeLabel(Node, PDG);
	}

	std::string getEdgeAttributes(const DDGNode *Node, typename GT::ChildIteratorType ChildNodeIt, const tsar::ProgramDependencyGraph *PDG) {
		assert((*ChildNodeIt.getCurrent())->getKind()!=DDGEdge::EdgeKind::Rooted);
		switch ((*ChildNodeIt.getCurrent())->getKind()) {
			case DDGEdge::EdgeKind::Unknown:
				return "style=\"dashed\"";
			case DDGEdge::EdgeKind::RegisterDefUse:
				return "style=\"solid\" color=\"blue\" "+BaseDOTGT::getEdgeAttributes(Node, ChildNodeIt, PDG);
			case DDGEdge::EdgeKind::MemoryDependence:
				return "style=\"solid\" color=\"green\" "+BaseDOTGT::getEdgeAttributes(Node, ChildNodeIt, PDG);
		}
	}
};

};

#endif//TSAR_INCLUDE_BUILDPDG_H