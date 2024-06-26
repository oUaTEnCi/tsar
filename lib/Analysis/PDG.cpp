#include "tsar/Analysis/PDG.h"
#include "tsar/Support/PassGroupRegistry.h"
#include "tsar/Core/Query.h"
#include <llvm/ADT/DepthFirstIterator.h>
#include <llvm/ADT/PostOrderIterator.h>
#include <llvm/ADT/EnumeratedArray.h>
#include <llvm/ADT/SCCIterator.h>
#include <llvm/IR/Dominators.h>
#include <llvm/InitializePasses.h>

using namespace tsar;
using namespace llvm;

#undef DEBUG_TYPE
#define DEBUG_TYPE "pdg"

STATISTIC(TotalGraphs, "Number of dependence graphs created.");
STATISTIC(TotalDefUseEdges, "Number of def-use edges created.");
STATISTIC(TotalMemoryEdges, "Number of memory dependence edges created.");
STATISTIC(TotalFineGrainedNodes, "Number of fine-grained nodes created.");
STATISTIC(TotalPiBlockNodes, "Number of pi-block nodes created.");
STATISTIC(TotalConfusedLLVMEdges, "Number of confused memory dependencies between two nodes.");
STATISTIC(TotalEdgeReversals, "Number of times the source and sink of dependence was reversed to expose cycles in the graph.");

using PDG=ProgramDependencyGraph;
using InstructionListType=SmallVector<Instruction*, 2>;

namespace llvm {
template<>
struct DOTGraphTraits<Function*>
    : public DOTGraphTraits<DOTFuncInfo*> {
  DOTGraphTraits(bool isSimple=false)
    : DOTGraphTraits<DOTFuncInfo*>(isSimple) {}
  std::string getNodeLabel(const BasicBlock *Node, Function*) {
    return DOTGraphTraits<DOTFuncInfo*>::getNodeLabel(Node, nullptr);
  }
  std::string getNodeAttributes(const BasicBlock *Node, Function *F) {
    DOTFuncInfo DOTInfo(F);
    return DOTGraphTraits<DOTFuncInfo*>::getNodeAttributes(Node, &DOTInfo);
  }
  std::string getEdgeAttributes(const BasicBlock *Node,
      const_succ_iterator EdgeIt, Function *F) {
    DOTFuncInfo DOTInfo(F);
    return DOTGraphTraits<DOTFuncInfo*>::getEdgeAttributes(Node, EdgeIt,
        &DOTInfo);
  }
  bool isNodeHidden(const BasicBlock *Node, const Function *F) {
    DOTFuncInfo DOTInfo(F);
    return DOTGraphTraits<DOTFuncInfo*>::isNodeHidden(Node, &DOTInfo);
  }
};

template<> struct GraphTraits<DomTreeNodeBase<tsar::SourceCFGNode>*>
    : public DomTreeGraphTraitsBase<DomTreeNodeBase<tsar::SourceCFGNode>,
    DomTreeNodeBase<tsar::SourceCFGNode>::const_iterator> {};

template<> struct GraphTraits<const DomTreeNodeBase<tsar::SourceCFGNode>*>
    : public DomTreeGraphTraitsBase<const DomTreeNodeBase<tsar::SourceCFGNode>,
    DomTreeNodeBase<tsar::SourceCFGNode>::const_iterator> {};

template<typename NodeValueType>
struct GraphTraits<PostDomTreeBase<NodeValueType>*>
    : public GraphTraits<DomTreeNodeBase<NodeValueType>*> {
private:
  using BaseGT=GraphTraits<DomTreeNodeBase<NodeValueType>*>;
public:
  static typename BaseGT::NodeRef getEntryNode(
      PostDomTreeBase<NodeValueType> *PDT) {
    return PDT->getRootNode();
  }
  static typename BaseGT::nodes_iterator nodes_begin(
      PostDomTreeBase<NodeValueType> *PDT) {
    return df_begin(getEntryNode(PDT));
  }
  static typename BaseGT::nodes_iterator nodes_end(
      PostDomTreeBase<NodeValueType> *PDT) {
    return df_end(getEntryNode(PDT));
  }
};

template<typename NodeValueType>
struct DOTGraphTraits<PostDomTreeBase<NodeValueType>*>
    : public DOTGraphTraits<typename PostDomTreeBase<NodeValueType>::
    ParentPtr> {
private:
  using BaseDOTGT=DOTGraphTraits<typename PostDomTreeBase<NodeValueType>::
      ParentPtr>;
public:
  DOTGraphTraits(bool IsSimple=false) : BaseDOTGT(IsSimple) {}
  static std::string getGraphName(const PostDomTreeBase<NodeValueType> *Tree) {
    return "Post-Dominator Tree";
  }
  std::string getNodeLabel(DomTreeNodeBase<NodeValueType> *Node,
      PostDomTreeBase<NodeValueType> *Tree) {
    if (Tree->isVirtualRoot(Node))
      return "Virtual Root";
    else
      return BaseDOTGT::getNodeLabel(Node->getBlock(), Node->getBlock()->
          getParent());
      
  }
  std::string getEdgeSourceLabel(DomTreeNodeBase<NodeValueType> *Node,
      typename GraphTraits<PostDomTreeBase<NodeValueType>*>::
      ChildIteratorType It) { return ""; }
  static bool isNodeHidden(DomTreeNodeBase<NodeValueType> *Node,
      PostDomTreeBase<NodeValueType> *Tree) {
    return false;
  }
  std::string getNodeAttributes(DomTreeNodeBase<NodeValueType> *Node,
      PostDomTreeBase<NodeValueType> *Tree) {
    if (Tree->isVirtualRoot(Node))
      return "";
    else
      return BaseDOTGT::getNodeAttributes(Node->getBlock(), Node->getBlock()->
          getParent());
  }
  std::string getEdgeAttributes(DomTreeNodeBase<NodeValueType>*,
      typename GraphTraits<PostDomTreeBase<NodeValueType>*>::ChildIteratorType,
      PostDomTreeBase<NodeValueType>*) {
    return "";
  }
};
}; //namespace llvm

namespace {
template<typename KeyT, typename ValueT, typename Container>
void addToMap(std::map<KeyT, Container> &Map, KeyT Key, ValueT Value) {
  auto It=Map.find(Key);
  if (It!=Map.end())
    Map[Key].insert(Value);
  else
    Map.insert({Key, {Value}});
}

template<typename NodeType>
bool hasEdgesTo(NodeType *SourceN, NodeType *TargetN) {
  for (auto ChildNodeIt=GraphTraits<NodeType*>::child_begin(SourceN);
    ChildNodeIt!=GraphTraits<NodeType*>::child_end(SourceN); ++ChildNodeIt)
    if (*ChildNodeIt==TargetN)
      return true;
  return false;
}
}; //anonymous namespace

template<typename CDGType>
inline void CDGBuilder<CDGType>::processControlDependence() {
  std::map<typename CDGType::NodeType*, std::set<NodeValueType>> DepInfo;
  for (auto NIt=GraphTraits<CFGType*>::nodes_begin(mCFG); NIt!=
      GraphTraits<CFGType*>::nodes_end(mCFG); ++NIt)
    mCDG->emplaceNode(*NIt);
  for (auto SrcNIt=++df_begin(mPDT.getRootNode()); SrcNIt!=
      df_end(mPDT.getRootNode()); ++SrcNIt) {
    if (SrcNIt->getBlock()==GraphTraits<CFGType*>::getEntryNode(mCFG))
      for (unsigned I=SrcNIt.getPathLength()-1; I>0; --I)
        addToMap(DepInfo, mCDG->getEntryNode(), SrcNIt.getPath(I)->getBlock());
    for (auto TgtNIt=++df_begin(SrcNIt->getIDom());
        TgtNIt!=df_end(SrcNIt->getIDom()); ++TgtNIt)
      if (hasEdgesTo(SrcNIt->getBlock(), TgtNIt->getBlock()))
        for (unsigned I=TgtNIt.getPathLength()-1; I>0; --I)
          addToMap(DepInfo, mCDG->getNode(SrcNIt->getBlock()),
              TgtNIt.getPath(I)->getBlock());
  }
  for (auto DIIt : DepInfo)
    for (auto TgtNIt : DIIt.second)
      mCDG->bindNodes(*DIIt.first, *mCDG->getNode(TgtNIt));
}

template<typename CDGType>
CDGType *CDGBuilder<CDGType>::populate(CFGType &CFG) {
  mCFG=&CFG;
  mCDG=new CDGType(std::string(CFG.getName()), &CFG);
  mPDT=PostDomTreeBase<CFGNodeType>();
  mPDT.recalculate(*mCFG);
  /*May be useful
  dumpDotGraphToFile(&mPDT, "post-dom-tree.dot", "post-dom-tree");
  */
  processControlDependence();
  return mCDG;
}

bool PDGNode::collectInstructions(function_ref<bool(Instruction*)> const &Pred,
    SmallVectorImpl<Instruction*> &IList) const {
  assert(IList.empty() && "Expected the IList to be empty on entry.");
  if (isa<SimplePDGNode>(this)) {
    for (Instruction *I : cast<const SimplePDGNode>(this)->getInstructions())
      if (Pred(I))
        IList.push_back(I);
  }
  else
    if (isa<PiBlockPDGNode>(this)) {
      for (const PDGNode *PN : cast<const PiBlockPDGNode>(this)->
          getInlinedNodes()) {
        assert(!isa<PiBlockPDGNode>(PN) &&
            "Nested PiBlocks are not supported.");
        SmallVector<Instruction*, 8> TmpIList;
        PN->collectInstructions(Pred, TmpIList);
        llvm::append_range(IList, TmpIList);
      }
    }
    else
      llvm_unreachable("unimplemented type of node");
  return !IList.empty();
}

void PDGNode::destroy() {
  switch (mKind) {
    case PDGNode::NodeKind::Entry:
      delete this;
      break;
    case PDGNode::NodeKind::SingleInstruction:
    case PDGNode::NodeKind::MultiInstruction:
      delete (SimplePDGNode*)this;
      break;
    case PDGNode::NodeKind::PiBlock:
      delete (PiBlockPDGNode*)this;
      break;
  }
}

PiBlockPDGNode::~PiBlockPDGNode() {
  for (PDGNode *N : mInlinedNodes) {
    for (PDGEdge *E : N->getEdges())
      E->destroy();
    N->destroy();
  }
}

void PDGEdge::destroy() {
  switch (mKind) {
    case PDGEdge::EdgeKind::RegisterDefUse:
    case PDGEdge::EdgeKind::Control:
      delete this;
      break;
    case PDGEdge::EdgeKind::MixedData:
    case PDGEdge::EdgeKind::Memory:
      delete (MemoryPDGEdge*)this;
      break;
    case PDGEdge::EdgeKind::ComplexData:
    case PDGEdge::EdgeKind::ComplexControl:
      delete (ComplexPDGEdge*)this;
      break;
  }
}

llvm::raw_ostream &tsar::operator<<(llvm::raw_ostream &OS, const PDGNode &N) {
  OS<<"Node Address:"<<&N<<":"<< N.getKind()<<"\n";
  if (isa<SimplePDGNode>(N)) {
    OS<<" Instructions:\n";
    for (const Instruction *I : cast<const SimplePDGNode>(N).getInstructions())
      OS.indent(2)<<*I<<"\n";
  }
  else
    if (isa<PiBlockPDGNode>(&N)) {
      OS<<"--- start of nodes in pi-block ---\n";
      const SmallVectorImpl<PDGNode*> &Nodes=cast<const PiBlockPDGNode>(&N)->
          getInlinedNodes();
      unsigned Count=0;
      for (const PDGNode *N : Nodes)
        OS<<*N<<(++Count==Nodes.size()?"":"\n");
      OS<<"--- end of nodes in pi-block ---\n";
    }
    else
      if (N.getKind()!=PDGNode::NodeKind::Entry)
        llvm_unreachable("Unimplemented type of node");
  OS<<(N.getEdges().empty()?" Edges:none!\n" : " Edges:\n");
  for (const PDGEdge *E : N.getEdges())
    OS.indent(2)<<*E;
  return OS;
}

llvm::raw_ostream &tsar::operator<<(llvm::raw_ostream &OS,
    const PDGNode::NodeKind K) {
  switch (K) {
    case PDGNode::NodeKind::Entry:
      OS<<"entry";
      break;
    case PDGNode::NodeKind::SingleInstruction:
      OS<<"single-instruction";
      break;
    case PDGNode::NodeKind::MultiInstruction:
      OS<<"multi-instruction";
      break;
    case PDGNode::NodeKind::PiBlock:
      OS<<"pi-block";
      break;
  }
  return OS;
}

llvm::raw_ostream &tsar::operator<<(llvm::raw_ostream &OS, const PDGEdge &E) {
  OS<<"["<< E.getKind()<<"] to "<<&E.getTargetNode()<<"\n";
    return OS;
}

llvm::raw_ostream &tsar::operator<<(llvm::raw_ostream &OS,
    const PDGEdge::EdgeKind K) {
  switch (K) {
    case PDGEdge::EdgeKind::RegisterDefUse:
      OS<<"def-use";
      break;
    case PDGEdge::EdgeKind::MixedData:
      OS<<"mixed data";
      break;
    case PDGEdge::EdgeKind::Memory:
      OS<<"memory";
      break;
    case PDGEdge::EdgeKind::Control:
      OS<<"control";
      break;
    case PDGEdge::EdgeKind::ComplexData:
      OS<<"complex data";
      break;
    case PDGEdge::EdgeKind::ComplexControl:
      OS<<"complex control";
      break;
  }
  return OS;
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const PDG &G) {
  for (const PDGNode *Node : G)
    OS<<*Node<<"\n";
  OS<<"\n";
  return OS;
}

//===--------------------------------------------------------------------===//
// PDG DOT Printer Implementation
//===--------------------------------------------------------------------===//
std::string PDGDotGraphTraits::getNodeLabel(const PDGNode *Node,
    const PDG *Graph) {
  if (isSimple())
    return getSimpleNodeLabel(Node, Graph);
  else
    return getVerboseNodeLabel(Node, Graph);
}

std::string PDGDotGraphTraits::getEdgeAttributes(const PDGNode *Node,
    GraphTraits<const PDGNode*>::ChildIteratorType I, const PDG *G) {
  const PDGEdge *E=static_cast<const PDGEdge*>(*I.getCurrent());
  if (isSimple())
    return getEdgeStyle(E);
  else
    return getEdgeStyle(E)+" "+getVerboseEdgeAttributes(Node, E, G);
}

bool PDGDotGraphTraits::isNodeHidden(const PDGNode *Node, const PDG *Graph) {
  if (const SimplePDGNode *SimpleNode=dyn_cast<SimplePDGNode>(Node)) {
    for (const Instruction *I : SimpleNode->getInstructions())
      if (!PDGBuilder::shouldShadow(*I))
        return false;
    return true;
  }
  else
    if (const PiBlockPDGNode *PiNode=dyn_cast<PiBlockPDGNode>(Node)) {
      for (const PDGNode *InlinedNode : PiNode->getInlinedNodes())
        if (!isNodeHidden(InlinedNode, Graph))
          return false;
      return true;
    }
    else
      return false;
}

std::string PDGDotGraphTraits::getSimpleNodeLabel(const PDGNode *Node,
    const PDG *G) {
  std::string Str;
  raw_string_ostream OS(Str);
  if (isa<SimplePDGNode>(Node)) {
    for (const Instruction *II : static_cast<const SimplePDGNode*>(Node)->
        getInstructions())
      if (!PDGBuilder::shouldShadow(*II))
        OS<<*II<<"\n";
  }
  else
    if (isa<PiBlockPDGNode>(Node)) {
      size_t NotHiddenNodesCount=0;
      for (const PDGNode *IN : cast<PiBlockPDGNode>(Node)->getInlinedNodes())
        if (!isNodeHidden(IN, G))
          ++NotHiddenNodesCount;
      OS<<"pi-block\nwith\n"<<NotHiddenNodesCount<<" nodes\n";
    }
    else
      if (Node->getKind()==PDGNode::NodeKind::Entry)
        OS<<"entry\n";
      else
        llvm_unreachable("Unimplemented type of node");
  return OS.str();
}

std::string PDGDotGraphTraits::getVerboseNodeLabel(const PDGNode *Node,
    const PDG *G) {
  std::string Str;
  raw_string_ostream OS(Str);
  OS<<"<kind:"<<Node->getKind()<<">\n";
  if (const SimplePDGNode *SimpleNode=dyn_cast<SimplePDGNode>(Node)) {
    for (const Instruction *II : SimpleNode->getInstructions())
      if (!PDGBuilder::shouldShadow(*II))
        OS<<*II<<"\n";
  }
  else
    if (const PiBlockPDGNode *PiNode=dyn_cast<PiBlockPDGNode>(Node)) {
      OS<<"--- start of nodes in pi-block ---\n";
      unsigned Count=0;
      for (const PDGNode *IN : PiNode->getInlinedNodes()) {
        ++Count;
        if (!isNodeHidden(IN, G))
          OS<<Count<<". "<<getVerboseNodeLabel(IN, G);
      }
      OS<<"--- end of nodes in pi-block ---\n";
    }
    else
      if (Node->getKind()==PDGNode::NodeKind::Entry)
        OS<<"entry\n";
      else
        llvm_unreachable("Unimplemented type of node");
  return OS.str();
}

std::string PDGDotGraphTraits::getVerboseEdgeAttributes(const PDGNode *Src,
    const PDGEdge *Edge, const PDG *G) {
  std::string Str;
  raw_string_ostream OS(Str);
  auto PrintMemoryEdge=[&OS](const MemoryPDGEdge &E) {
    if (llvm::Dependence * const *Dep=std::get_if<llvm::Dependence*>(&E.
        getMemoryDep())) {
      (*Dep)->dump(OS);
      OS.str().pop_back();
    }
    else {
      using namespace trait;
      auto PrintDIMemDep=[&OS](const MemoryPDGEdge::DIDepT &Dep) -> void {
        auto PrintDistVec=[&OS]<typename DepK>(const DIMemoryTraitRef &MT)
            -> void {
          const DIDependence *DistVec=MT->get<DepK>();
          OS<<DepK::toString()<<" - ";
          if (DistVec->getKnownLevel()==0)
            return;
          if (DistVec->getDistance(0).first==DistVec->getDistance(0).second)
            OS<<DistVec->getDistance(0).first;
          else
            OS<<"("<<DistVec->getDistance(0).first<<", "<<DistVec->
                getDistance(0).second<<")";
          for (int Level=1; Level<DistVec->getKnownLevel(); ++Level)
            if (DistVec->getDistance(Level).first==DistVec->
                getDistance(Level).second)
              OS<<", "<<DistVec->getDistance(Level).first;
            else
              OS<<", ("<<DistVec->getDistance(Level).first<<", "<<DistVec->
                  getDistance(Level).second<<")";
          OS<<">\n";
        };
        if (Dep.second.is<Flow>())
          PrintDistVec.operator()<Flow>(Dep.first);
        if (Dep.second.is<Anti>())
          PrintDistVec.operator()<Anti>(Dep.first);
        if (Dep.second.is<Output>())
          PrintDistVec.operator()<Output>(Dep.first);
        if (Dep.second.is<Private>())
          OS<<Private::toString()<<"\n";
        if (Dep.second.is<FirstPrivate>())
          OS<<FirstPrivate::toString()<<"\n";
        if (Dep.second.is<SecondToLastPrivate>())
          OS<<SecondToLastPrivate::toString()<<"\n";
        if (Dep.second.is<LastPrivate>())
          OS<<LastPrivate::toString()<<"\n";
        if (Dep.second.is<DynamicPrivate>())
          OS<<DynamicPrivate::toString()<<"\n";
        OS.str().pop_back();
      };
      const MemoryPDGEdge::DIDepStorageT &DIDeps=
          std::get<MemoryPDGEdge::DIDepStorageT>(E.getMemoryDep());
      for (auto &DIMemDep : DIDeps) {
        PrintDIMemDep(DIMemDep);
        OS<<"\n";
      }
    }
  };
  if (const MemoryPDGEdge *MemDepEdge=dyn_cast<MemoryPDGEdge>(Edge)) {
    OS<<"label=\"[";
    PrintMemoryEdge(*MemDepEdge);
    OS.str().pop_back();
    OS<<"]\"";
  }
  else
    if (const ComplexPDGEdge *ComplexEdge=dyn_cast<ComplexPDGEdge>(Edge)) {
      auto StringifyInlinedEdge=[&OS, &PrintMemoryEdge](const PDGEdge &E) ->
          std::string {
        using EdgeKind=PDGEdge::EdgeKind;
        std::string Str;
        raw_string_ostream OS(Str);
        switch (E.getKind()) {
          case EdgeKind::RegisterDefUse:
            return "def-use";
          case EdgeKind::MixedData:
            OS<<"def-use & ";
          case EdgeKind::Memory:
            PrintMemoryEdge(cast<MemoryPDGEdge>(E));
            return OS.str();
          case EdgeKind::Control:
            return "control";
          default:
            llvm_unreachable(
                "Only simple edges can be inlined in complex edge");
        }
      };
      OS<<"label=\"";
      for (const ComplexPDGEdge::EdgeHandle &EH : ComplexEdge->
          getInlinedEdges()) {
        if (!isNodeHidden(&EH.E.getTargetNode(), G))
          OS<<"("<<EH.SrcNOrdinal+1<<", "<<EH.TgtNordinal+1<<") "<<
              StringifyInlinedEdge(EH.E)<<"\n";
      }
      OS<<"\"";
    }
  return OS.str();
}

std::string PDGDotGraphTraits::getEdgeStyle(const tsar::PDGEdge *Edge) {
  switch (Edge->getKind()) {
    case tsar::PDGEdge::EdgeKind::RegisterDefUse:
      return "style=\"solid\" color=\"blue\"";
    case tsar::PDGEdge::EdgeKind::MixedData:
      return "style=\"solid\" color=\"purple\"";
    case tsar::PDGEdge::EdgeKind::Memory:
      return "style=\"solid\" color=\"green\"";
    case tsar::PDGEdge::EdgeKind::Control:
      return "style=\"dotted\"";
    case tsar::PDGEdge::EdgeKind::ComplexData:
      return "style=\"solid\" color=\"orchid\"";
    case tsar::PDGEdge::EdgeKind::ComplexControl:
      return "style=\"dashed\"";
  }
}
//===--------------------------------------------------------------------===//
// End of PDG DOT Printer Implementation
//===--------------------------------------------------------------------===//
PDGBuilder::PDGBuilder(PDG &G, const Function &F, DependenceInfo &DI,
    const AliasTree &AT, DIMemoryClientServerInfo &DIMInfo,
    const TargetLibraryInfo &TLI, const LoopInfo &LI, bool SolveReachability,
    bool Simplify, bool CreatePiBlocks)
    : mGraph(G), mF(F), mDI(DI), mAT(AT), mDIMInfo(DIMInfo), mTLI(TLI),
    mLI(LI), mSolvedReachability(SolveReachability), mSimplified(Simplify),
    mCreatedPiBlocks(CreatePiBlocks), mClientDIATRel(DIMInfo.ClientDIAT),
    mBBList(F.size()) {
  if (mDIMInfo.isValid())
    mServerDIATRel=SpanningTreeRelation<const DIAliasTree*>(mDIMInfo.DIAT);
  {
    size_t BBIdx=F.size();
    for (auto It=po_begin(&F); It!=po_end(&F); ++It)
      mBBList[--BBIdx]=*It;
  }
  if (SolveReachability)
    solveReachability();
  LLVM_DEBUG(
    {
    DOTFuncInfo DOTCFGInfo(&F);
    dumpDotGraphToFile(&DOTCFGInfo, "ircfg.dot", "control flow graph");
    if (!mDIMInfo.isValid())
      return;
    // Code below prints DIDependenceInfo,
    // which contains a set of DIAliasTraits for each loop in function
    // In each DIMemoryTrait it attempts to find NoAccess, ReadOnly, Shared,
    // Local, Private, FirstPrivate, SecondToLastPrivate, LastPrivate,
    // DynamicPrivate and, finally, Flow, Anti, Output
    auto PrintDIDepInfo=[](const DIDependencInfo &DIDepInfo) -> void {
      using namespace trait;
      dbgs()<<"==== Start of DIDependenceInfo Printing! ===\n";
      for (auto &DepInfoRecord : DIDepInfo) {
        for (const DIAliasTrait &AT : DepInfoRecord.second) {
          for (const DIMemoryTraitRef &MTRef : AT) {
            auto PrintDistanceVector=[](const trait::DIDependence *Dep)
                -> std::string {
              std::string Result;
              llvm::raw_string_ostream OS(Result);
              if (Dep->getKnownLevel()==0)
                return Result;
              OS<<"<";
              if (Dep->getDistance(0).first==Dep->getDistance(0).second)
                  OS<<Dep->getDistance(0).first;
              else
                OS<<"("<<Dep->getDistance(0).first<<", "<<
                    Dep->getDistance(0).second<<")";
              for (int Level=1; Level<Dep->getKnownLevel(); ++Level)
                if (Dep->getDistance(Level).first==
                    Dep->getDistance(Level).second)
                  OS<<", "<<Dep->getDistance(Level).first;
                else
                  OS<<", ("<<Dep->getDistance(Level).first<<", "<<
                      Dep->getDistance(Level).second<<")";
              OS<<">";
              return Result;
            };
            if (MTRef->is<NoAccess>())
              dbgs()<<"------------NoAccess\n";
            if (MTRef->is<Readonly>())
              dbgs()<<"------------Readonly\n";
            if (MTRef->is<Shared>())
              dbgs()<<"------------Shared\n";
            /*if (MTRef->is<Local>())
               dbgs()<<"  -   -   -Local\n";*/
            if (MTRef->is<Private>())
              dbgs()<<"------------Private\n";
            if (MTRef->is<FirstPrivate>())
              dbgs()<<"------------FirstPrivate\n";
            if (MTRef->is<SecondToLastPrivate>())
              dbgs()<<"------------SecondToLastPrivate\n";
            if (MTRef->is<LastPrivate>())
              dbgs()<<"------------LastPrivate\n";
            if (MTRef->is<DynamicPrivate>())
              dbgs()<<"------------DynamicPrivate\n";
            trait::DIDependence *DIFlow=MTRef->get<trait::Flow>();
            trait::DIDependence *DIAnti=MTRef->get<trait::Anti>();
            trait::DIDependence *DIOutput=MTRef->get<trait::Output>();
            if (DIFlow)
              dbgs()<<"------------Flow, "<<PrintDistanceVector(DIFlow)<<
                  ", causes size - "<<DIFlow->getCauses().size()<<"\n";
            if (DIAnti)
              dbgs()<<"------------Anti, "<<PrintDistanceVector(DIAnti)<<
                  ", causes size - "<<DIAnti->getCauses().size()<<"\n";
            if (DIOutput)
              dbgs()<<"------------Output, "<<PrintDistanceVector(DIOutput)
                  <<", causes size - "<<DIOutput->getCauses().size()<<"\n";
            dbgs()<<"--------end of di-memory-trait hasNoDep - "<<
                hasNoDep(*MTRef)<<", hasSpuriosDep - "<<hasSpuriousDep(*MTRef)
                <<"\n";
          }
          dbgs()<<"----end of di-alias-trait\n";
        }
        dbgs()<<"end of di-set-record (pair::MDNode*, DIDependenceSet)\n";
      }
      dbgs()<<"=== End of DIDependenceInfo Printing ===\n";
    };
    PrintDIDepInfo(*mDIMInfo.DIDepInfo);
    }
  );
}

void PDGBuilder::computeInstructionOrdinals() {
  // The BBList is expected to be in program order.
  size_t NextOrdinal=1;
  for (const BasicBlock *BB : mBBList) {
    assert(BB);
    for (const Instruction &I : *BB)
      mInstOrdinalMap.insert(std::make_pair(&I, NextOrdinal++));
  }
}

void PDGBuilder::createFineGrainedNodes() {
  ++TotalGraphs;
  assert(mIMap.empty() && "Expected empty instruction map at start");
  for (const BasicBlock *BB : mBBList)
    for (const Instruction &I : *BB) {
      auto &NewNode=createFineGrainedNode(I);
      mIMap.insert(std::make_pair(&I, &NewNode));
      mNodeOrdinalMap.insert(std::make_pair(&NewNode, getOrdinal(I)));
      ++TotalFineGrainedNodes;
    }
}

void PDGBuilder::createDefUseEdges() {
  for (PDGNode *N : mGraph) {
    if (N->getKind()==PDGNode::NodeKind::Entry)
      continue;
    SimplePDGNode &InstrNode=*cast<SimplePDGNode>(N);
    Instruction &VI=*InstrNode.getFirstInstruction();
    // Use a set to mark the targets that we link to N, so we don't add
    // duplicate def-use edges when more than one instruction in a target node
    // use results of instructions that are contained in N.
    SmallPtrSet<PDGNode*, 4> VisitedTargets;
    for (User *U : VI.users()) {
      Instruction*UI =dyn_cast<Instruction>(U);
      if (!UI)
        continue;
      PDGNode *DstNode;
      if (mIMap.find(UI)!=mIMap.end())
        DstNode=mIMap.find(UI)->second;
      else {
        // In the case of loops, the scope of the subgraph is all the
        // basic blocks (and instructions within them) belonging to the loop. We
        // simply ignore all the edges coming from (or going into) instructions
        // or basic blocks outside of this range.
        LLVM_DEBUG(dbgs()<<"skipped def-use edge since the sink"<<*UI<<
            " is outside the range of instructions being considered.\n");
        continue;
      }
      // Self dependencies are ignored because they are redundant and
      // uninteresting.
      if (DstNode==N) {
        LLVM_DEBUG(dbgs()<<
            "skipped def-use edge since the sink and the source ("<<N<<
            ") are the same.\n");
        continue;
      }
      if (VisitedTargets.insert(DstNode).second) {
        createDefUseEdge(*N, *DstNode);
        ++TotalDefUseEdges;
      }
    }
  }
}

void PDGBuilder::createMemoryDependenceEdges() {
  for (auto SrcNodeIt=mGraph.begin(); SrcNodeIt!=mGraph.end(); ++SrcNodeIt) {
    if ((*SrcNodeIt)->getKind()==PDGNode::NodeKind::Entry)
      continue;
    SimplePDGNode &SrcInstrNode=*cast<SimplePDGNode>(*SrcNodeIt);
    Instruction &SrcInstr=*SrcInstrNode.getFirstInstruction();
    if (!SrcInstr.mayReadOrWriteMemory())
      continue;
    for (auto DstNodeIt=SrcNodeIt; DstNodeIt!=mGraph.end(); ++DstNodeIt) {
      SmallVector<PDGEdge*, 1> DefUseEdges;
      auto CreateDepEdge=[&DefUseEdges, this](PDGNode &Src, PDGNode &Tgt,
          const MemoryPDGEdge::MemDepHandle &Dep) {
        if (Src.findEdgesTo(Tgt, DefUseEdges)) {
          assert(DefUseEdges.size()==1);
          Src.removeEdge(*DefUseEdges.front());
          DefUseEdges.front()->destroy();
          DefUseEdges.clear();
          assert(mGraph.connect(Src, Tgt, *(new MemoryPDGEdge(Tgt, Dep,
              true))));
        }
        else
          assert(mGraph.connect(Src, Tgt, *(new MemoryPDGEdge(Tgt, Dep,
              false))));
        ++TotalMemoryEdges;
      };
      if (**SrcNodeIt==**DstNodeIt)
        continue;
      SimplePDGNode &DstInstrNode=*cast<SimplePDGNode>(*DstNodeIt);
      Instruction &DstInstr=*DstInstrNode.getFirstInstruction();
      if (!DstInstr.mayReadOrWriteMemory())
        continue;
      if (!(SrcInstr.getParent()==DstInstr.getParent() ||
          isReachable(*SrcInstr.getParent(), *DstInstr.getParent()) ||
          isReachable(*DstInstr.getParent(), *SrcInstr.getParent())))
        continue;
      std::unique_ptr<llvm::Dependence> Dep=mDI.depends(&SrcInstr, &DstInstr,
          true);
      MemoryPDGEdge::DIDepStorageT ForwardDIDep, BackwardDIDep;
      if (!Dep)
        continue;
      // If we have a dependence with its left-most non-'=' direction
      // being '>' we need to reverse the direction of the edge, because
      // the source of the dependence cannot occur after the sink. For
      // confused dependencies, we will create edges in both directions to
      // represent the possibility of a cycle.
      if (Dep->isConfused() && confirmMemoryIntersect(SrcInstr, DstInstr,
          ForwardDIDep, BackwardDIDep)) {
        if (!ForwardDIDep.empty() || !BackwardDIDep.empty()) {
          if (!ForwardDIDep.empty())
            CreateDepEdge(**SrcNodeIt, **DstNodeIt, ForwardDIDep);
          if (!BackwardDIDep.empty()) {
            CreateDepEdge(**DstNodeIt, **SrcNodeIt, BackwardDIDep);
            ++TotalEdgeReversals;
          }
        }
        else {
          /*CreateDepEdge(**SrcNodeIt, **DstNodeIt, new Dependence(&SrcInstr,
              &DstInstr));
          CreateDepEdge(**DstNodeIt, **SrcNodeIt, new Dependence(&DstInstr,
              &SrcInstr));*/
          CreateDepEdge(**SrcNodeIt, **DstNodeIt, Dep.release());
          CreateDepEdge(**DstNodeIt, **SrcNodeIt, mDI.depends(&DstInstr,
              &SrcInstr, true).release());
          ++TotalConfusedLLVMEdges;
        }
        continue;
      }
      if (Dep->isOrdered()) {
        if (!Dep->isLoopIndependent()) {
          bool ReversedEdge=false;
          for (unsigned Level=1; Level<=Dep->getLevels(); ++Level) {
            if (Dep->getDirection(Level)==Dependence::DVEntry::EQ)
              continue;
            if (Dep->getDirection(Level) == Dependence::DVEntry::GT) {
              CreateDepEdge(**DstNodeIt, **SrcNodeIt, Dep.release());
              ReversedEdge=true;
              ++TotalEdgeReversals;
              break;
            }
            if (Dep->getDirection(Level)==Dependence::DVEntry::LT)
              break;
            CreateDepEdge(**SrcNodeIt, **DstNodeIt, Dep.release());
            CreateDepEdge(**DstNodeIt, **SrcNodeIt, mDI.depends(&DstInstr,
                &SrcInstr, true).release());
            ReversedEdge=true;
            ++TotalConfusedLLVMEdges;
            break;
          }
          if (!ReversedEdge)
            CreateDepEdge(**SrcNodeIt, **DstNodeIt, Dep.release());
        }
        else
          CreateDepEdge(**SrcNodeIt, **DstNodeIt, Dep.release());
      }
    }
  }
}

void PDGBuilder::createControlDependenceEdges() {
  IRCDG *CDG=CDGBuilder<IRCDG>().populate(const_cast<Function&>(mF));
  PDGNode &EntryPDGNode=*(new PDGNode());
  mGraph.addEntryNode(EntryPDGNode);
  for (IRCDGNode *BBNode : *CDG) {
    auto LinkControlDependent=[this](PDGNode &SrcNode,
        IRCDGNode::EdgeListTy &CDGDependentNodes) {
      for (IRCDGEdge *ControlDependence : CDGDependentNodes) {
        BasicBlock *TargetBlock=cast<DefaultIRCDGNode>(&ControlDependence->
            getTargetNode())->getBlock();
        for (Instruction &I : *TargetBlock) {
          auto InstrPDGNodeIt=mIMap.find(&I);
          if (InstrPDGNodeIt!=mIMap.end()) {
            PDGEdge &NewPDGEdge=*(new PDGEdge(*InstrPDGNodeIt->second,
                PDGEdge::DependenceType::Control));
            assert(mGraph.connect(SrcNode, *InstrPDGNodeIt->second,
                NewPDGEdge));
          }
        }
      }
    };
    if (isa<EntryIRCDGNode>(BBNode))
      LinkControlDependent(EntryPDGNode, BBNode->getEdges());
    else {
      DefaultIRCDGNode &SrcNode=*cast<DefaultIRCDGNode>(BBNode);
      assert(mIMap.find(SrcNode.getBlock()->getTerminator())!=mIMap.end());
      LinkControlDependent(*mIMap[SrcNode.getBlock()->getTerminator()],
          SrcNode.getEdges());
    }
  }
  delete CDG;
}

void PDGBuilder::simplify() {
  if (!mSimplified)
    return;
  LLVM_DEBUG(dbgs()<<"==== Start of Graph Simplification ===\n");
  // This algorithm works by first collecting a set of candidate nodes that have
  // an out-degree of one (in terms of def-use edges), and then ignoring those
  // whose targets have an in-degree more than one. Each node in the resulting
  // set can then be merged with its corresponding target and put back into the
  // worklist until no further merge candidates are available.
  DenseSet<PDGNode*> CandidateSourceNodes;
  // A mapping between nodes and their in-degree. To save space, this map
  // only contains nodes that are targets of nodes in the CandidateSourceNodes.
  DenseMap<PDGNode*, unsigned> TargetInDegreeMap;
  for (PDGNode *N : mGraph) {
    if (N->getEdges().size()!=1)
      continue;
    PDGEdge &Edge=N->back();
    if (Edge.getKind()!=PDGEdge::EdgeKind::RegisterDefUse)
      continue;
    CandidateSourceNodes.insert(N);
    // Insert an element into the in-degree map and initialize to zero. The
    // count will get updated in the next step.
    TargetInDegreeMap.insert({&Edge.getTargetNode(), 0});
  }
  LLVM_DEBUG({
    dbgs()<<"Size of candidate src node list:"<<CandidateSourceNodes.size()
        <<"\nNode with single outgoing def-use edge:\n";
    for (PDGNode *N : CandidateSourceNodes)
      dbgs()<<N<<"\n";
  });
  for (PDGNode *N : mGraph) {
    SmallPtrSet<PDGNode*, 4> CDependentTargets;
    for (PDGEdge *E : *N) {
      auto RegisterCDNode=[&CDependentTargets](PDGNode &N) {
        if (CDependentTargets.contains(&N))
          CDependentTargets.erase(&N);
        else
          CDependentTargets.insert(&N);
      };
      PDGNode &TgtNode=E->getTargetNode();
      auto TgtIt=TargetInDegreeMap.find(&TgtNode);
      if (E->isControl()) {
        auto SrcIt=CandidateSourceNodes.find(&TgtNode);
        if (SrcIt!=CandidateSourceNodes.end())
          RegisterCDNode(TgtNode.back().getTargetNode());
        if (TgtIt!=TargetInDegreeMap.end())
          RegisterCDNode(TgtNode);
      }
      else
        if (TgtIt!=TargetInDegreeMap.end())
          ++(TgtIt->second);
    }
    for (PDGNode *Target : CDependentTargets)
      TargetInDegreeMap[Target]=2;
  }
  LLVM_DEBUG({
    dbgs()<<"Size of target in-degree map:"<<TargetInDegreeMap.size()
      <<"\nContent of in-degree map:\n";
    for (auto &I : TargetInDegreeMap) {
      dbgs()<<I.first<<" --> "<<I.second<<"\n";
    }
  });
  SmallVector<PDGNode*, 32> Worklist(CandidateSourceNodes.begin(),
      CandidateSourceNodes.end());
  while (!Worklist.empty()) {
    PDGNode &Src=*Worklist.pop_back_val();
    // As nodes get merged, we need to skip any node that has been removed from
    // the candidate set (see below).
    if (!CandidateSourceNodes.erase(&Src))
      continue;
    assert(Src.getEdges().size()==1 &&
      "Expected a single edge from the candidate src node.");
    PDGNode &Tgt=Src.back().getTargetNode();
    assert(TargetInDegreeMap.find(&Tgt)!=TargetInDegreeMap.end() &&
      "Expected target to be in the in-degree map.");
    // Do not merge if there is also an edge from target to src (immediate
    // cycle).
    if (TargetInDegreeMap[&Tgt]!=1 || !areNodesMergeable(Src, Tgt) ||
        Tgt.hasEdgeTo(Src))
      continue;
    LLVM_DEBUG(dbgs()<<"Merging:"<<Src<<"\nWith:"<<Tgt<<"\n");
    mergeNodes(Src, Tgt);
    // If the target node is in the candidate set itself, we need to put the
    // src node back into the worklist again so it gives the target a chance
    // to get merged into it. For example if we have:
    // {(a)->(b), (b)->(c), (c)->(d), ...} and the worklist is initially {b, a},
    // then after merging (a) and (b) together, we need to put (a,b) back in
    // the worklist so that (c) can get merged in as well resulting in
    // {(a,b,c) -> d}
    // We also need to remove the old target (b), from the worklist. We first
    // remove it from the candidate set here, and skip any item from the
    // worklist that is not in the set.
    if (CandidateSourceNodes.erase(&Tgt)) {
      Worklist.push_back(&Src);
      CandidateSourceNodes.insert(&Src);
      LLVM_DEBUG(dbgs()<<"Putting "<<&Src<<" back in the worklist.\n");
    }
  }
  LLVM_DEBUG(dbgs()<<"=== End of Graph Simplification ===\n");
}

void PDGBuilder::createPiBlocks() {
  using NodeListType=SmallVector<PDGNode*, 4>;
  if (!mCreatedPiBlocks)
    return;
  LLVM_DEBUG(dbgs()<<"==== Start of Creation of Pi-Blocks ===\n");
  // The overall algorithm is as follows:
  // 1. Identify SCCs and for each SCC create a pi-block node containing all
  //    the nodes in that SCC.
  // 2. Identify incoming edges incident to the nodes inside of the SCC and
  //    reconnect them to the pi-block node.
  // 3. Identify outgoing edges from the nodes inside of the SCC to nodes
  //    outside of it and reconnect them so that the edges are coming out of the
  //    SCC node instead.

  // Adding nodes as we iterate through the SCCs cause the SCC
  // iterators to get invalidated. To prevent this invalidation, we first
  // collect a list of nodes that are part of an SCC, and then iterate over
  // those lists to create the pi-block nodes. Each element of the list is a
  // list of nodes in an SCC. Note: trivial SCCs containing a single node are
  // ignored.
  SmallVector<NodeListType, 4> ListOfSCCs;
  for (const std::vector<PDGNode*> &SCC : make_range(scc_begin(&mGraph),
      scc_end(&mGraph)))
    if (SCC.size()>1)
      ListOfSCCs.emplace_back(SCC.begin(), SCC.end());
  for (NodeListType &NL : ListOfSCCs) {
    LLVM_DEBUG(dbgs()<<"Creating pi-block node with "<<NL.size()<<
        " nodes in it.\n");
    // SCC iterator may put the nodes in an order that's different from the
    // program order. To preserve original program order, we sort the list of
    // nodes based on ordinal numbers computed earlier.
    llvm::sort(NL, [&](PDGNode *LHS, PDGNode *RHS) {
      return getOrdinal(*LHS)<getOrdinal(*RHS);
    });
    PDGNode &PiNode=createPiBlock(NL);
    ++TotalPiBlockNodes;
    // Build a set to speed up the lookup for edges whose targets
    // are inside the SCC.
    SmallPtrSet<PDGNode*, 4> NodesInSCC(NL.begin(), NL.end());
    // We have the set of nodes in the SCC. We go through the set of nodes
    // that are outside of the SCC and look for edges that cross the two sets.
    for (auto ExternalNodeIt=mGraph.begin(); ExternalNodeIt!=mGraph.end();) {
      // Skip the SCC node.
      if (*ExternalNodeIt==&PiNode) {
        ++ExternalNodeIt;
        continue;
      }
      // Remove SCC inlined node from graph's node list and skip it.
      if (NodesInSCC.count(*ExternalNodeIt)) {
        mGraph.Nodes.erase(ExternalNodeIt);
        continue;
      }
      // Use these flags to help us avoid creating redundant edges. If there
      // are more than one edges from an outside node to inside nodes, we only
      // keep one edge from that node to the pi-block node. Similarly, if
      // there are more than one edges from inside nodes to an outside node,
      // we only keep one edge from the pi-block node to the outside node.
      // There is a flag defined for each direction (incoming vs outgoing) and
      // for each type of edge supported, using a two-dimensional boolean
      // array.
      using Direction=typename ComplexPDGEdge::Direction;
      using EdgeKind=typename PDGEdge::EdgeKind;
      using DependenceType=typename PDGEdge::DependenceType;
      EnumeratedArray<ComplexPDGEdge*, DependenceType>
          NewEdges[Direction::DirectionCount] {nullptr, nullptr};
      for (size_t SCCNodeI=0; SCCNodeI<NL.size(); ++SCCNodeI) {
        auto ReconnectEdges=[this, &PiNode, &NewEdges, SCCNodeI]
            (PDGNode &Src, PDGNode &Dst, const Direction Dir) {
          SmallVector<PDGEdge*, 10> EL;
          Src.findEdgesTo(Dst, EL);
          if (EL.empty())
            return;
          LLVM_DEBUG(dbgs()<<"reconnecting("<<(Dir==Direction::Incoming?
              "incoming)":"outgoing)")<<":\nSrc:"<<Src<<"\nDst:"<<Dst<<
              "\nNew:"<<PiNode<<"\n");
          for (PDGEdge *OldEdge : EL) {
            DependenceType Type=OldEdge->getDependenceType();
            if (NewEdges[Dir][Type]) {
              NewEdges[Dir][Type]->absorbEdge(*OldEdge, SCCNodeI, Dir);
              LLVM_DEBUG(dbgs()<<"absorbed old edge between Src and Dst.\n\n");
            }
            else {
              if (Dir==Direction::Incoming) {
                NewEdges[Dir][Type]=new ComplexPDGEdge(PiNode, *OldEdge,
                    SCCNodeI, Dir);
                mGraph.connect(Src, PiNode, *NewEdges[Dir][Type]);
                LLVM_DEBUG(dbgs()<<
                    "created complex edge from Src to PiNode.\n");
              }
              else {
                NewEdges[Dir][Type]=new ComplexPDGEdge(Dst, *OldEdge, SCCNodeI,
                    Dir);
                mGraph.connect(PiNode, Dst, *NewEdges[Dir][Type]);
                LLVM_DEBUG(dbgs()<<
                    "created complex edge from PiNode to Dst.\n");
              }
            }
            Src.removeEdge(*OldEdge);
            LLVM_DEBUG(dbgs()<<"removed old edge between Src and Dst.\n\n");
            /*if (OldEdge->getKind()==PDGEdge::EdgeKind::ComplexControl
                || OldEdge->getKind()==PDGEdge::EdgeKind::ComplexData) {
              OldEdge->destroy();
              LLVM_DEBUG(dbgs()<<"released memory from old complex edge \\
                  between Src and Dst.\n\n");
            }*/
          }
        };
        // Process incoming edges incident to the pi-block node.
        ReconnectEdges(**ExternalNodeIt, *NL[SCCNodeI], Direction::Incoming);
        // Process edges that are coming out of the pi-block node.
        ReconnectEdges(*NL[SCCNodeI], **ExternalNodeIt, Direction::Outgoing);
      }
      ++ExternalNodeIt;
    }
  }
  // Ordinal maps are no longer needed.
  mInstOrdinalMap.clear();
  mNodeOrdinalMap.clear();
  LLVM_DEBUG(dbgs()<<"==== End of Creation of Pi-Blocks ===\n");
}

bool PDGBuilder::confirmMemoryIntersect(const Instruction &SrcInst,
    const Instruction &DstInst, MemoryPDGEdge::DIDepStorageT &NewForwardDep,
    MemoryPDGEdge::DIDepStorageT &NewBackwardDep) {
  bool SrcUnknownMemory=false, DstUnknownMemory=false,
    *CurrMarker=&SrcUnknownMemory;
  MemoryLocationSet SrcMemLocs, DstMemLocs, *CurrSet=&SrcMemLocs;
  auto CollectMemory=[&CurrSet](Instruction &I, MemoryLocation &&MemLoc,
    unsigned OpInd, AccessInfo IsRead, AccessInfo IsWrite) {
    CurrSet->insert(MemLoc);
  };
  auto EvaluateUnknown=[&CurrMarker](Instruction&, AccessInfo, AccessInfo) {
    *CurrMarker=true;
  };
  for_each_memory(const_cast<Instruction&>(SrcInst),
      const_cast<TargetLibraryInfo&>(mTLI), CollectMemory, EvaluateUnknown);
  CurrSet=&DstMemLocs;
  CurrMarker=&DstUnknownMemory;
  for_each_memory(const_cast<Instruction&>(DstInst),
      const_cast<TargetLibraryInfo&>(mTLI), CollectMemory, EvaluateUnknown);
  if (SrcMemLocs.empty() || DstMemLocs.empty())
    return !(SrcMemLocs.empty() && !SrcUnknownMemory || DstMemLocs.empty() &&
        !DstUnknownMemory);
  enum InfoType {
    Client,
    Server,
    Last=Server
  };
  EnumeratedArray<SmallVector<const DIMemory*, 2>, InfoType> SrcDIMems,
      DstDIMems;
  /*EnumeratedArray<SmallSetVector<const DIMemory*, 2>, InfoType> SrcDIMems,
      DstDIMems;*/
  auto FillDIMemories=[this, &SrcInst](const MemoryLocationSet &MemLocs,
      SmallVectorImpl<const DIMemory*> &ClientDIMems,
      SmallVectorImpl<const DIMemory*> &ServerDIMems) {
    for (auto &MemLoc : MemLocs) {
      const EstimateMemory *EstMem=mAT.find(MemLoc);
      const MDNode *Metadata;
      while (!(Metadata=getRawDIMemoryIfExists(*EstMem, mF.getContext(),
          mF.getParent()->getDataLayout(), mAT.getDomTree())))
        EstMem=EstMem->getParent();
      ClientDIMems.push_back(&*mDIMInfo.ClientDIAT->find(*Metadata));
      if (mDIMInfo.isServerAvailable())
        ServerDIMems.push_back(mDIMInfo.findFromClient(*EstMem,
            SrcInst.getModule()->getDataLayout(),
            const_cast<DominatorTree&>(mAT.getDomTree())).get<Clone>());
    }
  };
  FillDIMemories(SrcMemLocs, SrcDIMems[Client], SrcDIMems[Server]);
  FillDIMemories(DstMemLocs, DstDIMems[Client], DstDIMems[Server]);
  bool HasDependence=false;
  EnumeratedArray<bool, InfoType> FoundDINode(false);
  for (int SrcI=0; SrcI<SrcDIMems[Client].size() && !HasDependence; ++SrcI) {
    EnumeratedArray<const DIAliasNode*, InfoType> SrcDINode(nullptr);
    for (int DstI=0; DstI<DstDIMems[Client].size() && !HasDependence; ++DstI) {
      EnumeratedArray<const DIAliasNode*, InfoType> DstDINode(nullptr);
      if (
        mDIMInfo.isServerAvailable() &&
        SrcDIMems[Server][SrcI] &&
        DstDIMems[Server][DstI] &&
        (SrcDINode[Server]=SrcDIMems[Server][SrcI]->getAliasNode()) &&
        (DstDINode[Server]=DstDIMems[Server][DstI]->getAliasNode())
        ) {
        if (mServerDIATRel.value().compare(SrcDINode[Server],
            DstDINode[Server])!=TreeRelation::TR_UNREACHABLE)
          HasDependence=true;
      }
      else
        if (
          SrcDIMems[Client][SrcI] &&
          DstDIMems[Client][DstI] &&
          (SrcDINode[Client]=SrcDIMems[Client][SrcI]->getAliasNode()) &&
          (DstDINode[Client]=DstDIMems[Client][DstI]->getAliasNode()) &&
          mClientDIATRel.compare(SrcDINode[Client], DstDINode[Client])!=
              TreeRelation::TR_UNREACHABLE)
          HasDependence=true;
      FoundDINode[Client]|=SrcDINode[Client] && DstDINode[Client];
      FoundDINode[Server]|=SrcDINode[Server] && DstDINode[Server];
    }
  }
  if (!HasDependence && (FoundDINode[Client] || FoundDINode[Server]))
    return false;
  // At this point we have memory intersection confirmed by AliasTrees,
  // but this fact doesn't mean the presence of loop-carried dependence, so we
  // need to observe trait set
  auto FindDIDependencies=[this, &SrcInst, &DstInst, &NewForwardDep,
      &NewBackwardDep](
      const SmallVectorImpl<const DIMemory*> &SrcDIMems,
      const SmallVectorImpl<const DIMemory*> &DstDIMems,
      const SpanningTreeRelation<const DIAliasTree*> &DIATRel) -> bool {
    using namespace trait;
    using EdgeTraitInfoT=SmallDenseMap<DIMemoryTraitRef, MemoryDescriptor>;
    auto FindCommonLoop=[](const Loop *&SrcL, const Loop *&DstL)
        -> const Loop* {
      if (!SrcL || !DstL)
        return nullptr;
      if (SrcL->getLoopDepth()<DstL->getLoopDepth())
        for (unsigned LargerNL=DstL->getLoopDepth(); LargerNL>SrcL->
            getLoopDepth(); --LargerNL)
          DstL=DstL->getParentLoop();
      else
        for (unsigned LargerNL=SrcL->getLoopDepth(); LargerNL>DstL->
            getLoopDepth(); --LargerNL)
          SrcL=SrcL->getParentLoop();
      while (SrcL!=DstL) {
        SrcL=SrcL->getParentLoop();
        DstL=DstL->getParentLoop();
      }
      if (SrcL) {
        const Loop *Result=SrcL;
        SrcL=SrcL->getParentLoop();
        DstL=DstL->getParentLoop();
        return Result;
      }
      return nullptr;
    };
    auto AddTrait=[]<typename DepTrait>(EdgeTraitInfoT &EdgeTraits,
        const DIMemoryTraitRef &Access) -> void {
      auto It=EdgeTraits.find(Access);
      if (It!=EdgeTraits.end())
        It->second.set<DepTrait>();
      else {
        MemoryDescriptor NewDK;
        NewDK.set<DepTrait>();
        EdgeTraits.insert({Access, NewDK});
      }
    };
    EdgeTraitInfoT ForwTraits, BackwTraits;
    bool FoundDep=false, FoundTrait=false;
    const Loop *SrcL=mLI[SrcInst.getParent()], *DstL=mLI[DstInst.getParent()];
    // Attempting to arrange Instruction's DI-mems sets for possible better
    // dependence analysis
    const SmallVectorImpl<const DIMemory*> *LowerMem=&SrcDIMems,
        *HigherMem=&DstDIMems;
    for (int ExitFlag=0, SrcI=0; SrcI<SrcDIMems.size() && !ExitFlag; ++SrcI)
      for (int DstI=0; DstI<DstDIMems.size() && !ExitFlag; ++DstI) {
        const DIAliasNode *SrcAliasN, *DstAliasN;
        if (SrcDIMems[SrcI] && DstDIMems[DstI] && (SrcAliasN=SrcDIMems[SrcI]->
            getAliasNode()) && (DstAliasN=DstDIMems[DstI]->getAliasNode()))
          if (DIATRel.compare(SrcAliasN, DstAliasN)==
              TreeRelation::TR_ANCESTOR) {
            LowerMem=&DstDIMems;
            HigherMem=&SrcDIMems;
            ExitFlag=1;
          }
          else
            if (DIATRel.compare(SrcAliasN, DstAliasN)==
              TreeRelation::TR_DESCENDANT) {
              LowerMem=&SrcDIMems;
              HigherMem=&DstDIMems;
              ExitFlag=1;
            }
      }
    while (const Loop *CommonLoop=FindCommonLoop(SrcL, DstL)) {
      const DIDependenceSet *DIDepSet=mDIMInfo.findFromClient(*CommonLoop);
      if (!DIDepSet)
        continue;
      for (const DIMemory *LoMem : *LowerMem)
        for (const DIAliasTrait &AT : *DIDepSet) {
          auto CheckCauses=[&SrcInst, &DstInst, &ForwTraits, &BackwTraits,
              &AddTrait]<typename DepK>(const DIMemoryTraitRef &Access)
              -> bool {
            DIDependence *Dep=Access->get<DepK>();
            if (!Dep)
              return false;
            for (const DIDependence::Cause &C : Dep->getCauses())
              if (C.get<DebugLoc>())
                if (SrcInst.getDebugLoc() && C.get<DebugLoc>()==
                    SrcInst.getDebugLoc()) {
                  AddTrait.operator()<DepK>(ForwTraits, Access);
                  return true;
                }
                else
                  if (DstInst.getDebugLoc() && C.get<DebugLoc>()==
                      DstInst.getDebugLoc()) {
                    AddTrait.operator()<DepK>(BackwTraits, Access);
                    return true;
                  }
            return false;
          };
          auto CheckPrivateTrait=[&ForwTraits, &BackwTraits, &AddTrait]
              <typename DepTrait>(const DIMemoryTraitRef &Access) -> bool {
            if (Access->is<DepTrait>()) {
              AddTrait.operator()<DepTrait>(ForwTraits, Access);
              AddTrait.operator()<DepTrait>(BackwTraits, Access);
              return true;
            }
            return false;
          };
          auto MemAccessIt=AT.find(LoMem);
          if (MemAccessIt==AT.end())
            continue;
          const DIMemoryTraitRef &MemAccess=*MemAccessIt;
          auto HiMemIt=std::find(HigherMem->begin(), HigherMem->end(),
              MemAccess->getMemory());
          if (HiMemIt==HigherMem->end())
            continue;
          // 1.: NoAccess, Readonly, Shared - No dependencies
          // It is worth to notice, that hasSpuriosDep really excludes some
          // of deps (mostly containing unions of private-like and shared
          // traits), which would have been showed otherwise
          FoundTrait=true;
          if (hasNoDep(*MemAccess) /*|| hasSpuriousDep(*MemAccess)*/)
            continue;
          FoundDep=true;
          // 2.: Private, FirstPrivate, SecondToLastPrivate, LastPrivate,
          // DynamicPrivate - All dependencies
          if (CheckPrivateTrait.operator()<Private>(MemAccess) ||
              CheckPrivateTrait.operator()<FirstPrivate>(MemAccess) ||
              CheckPrivateTrait.operator()<SecondToLastPrivate>(MemAccess) ||
              CheckPrivateTrait.operator()<LastPrivate>(MemAccess) ||
              CheckPrivateTrait.operator()<DynamicPrivate>(MemAccess))
            continue;
          // 3.: Flow, Anti, Output - Concrete dependence
          if (CheckCauses.operator()<Flow>(MemAccess) ||
              CheckCauses.operator()<Anti>(MemAccess) ||
              CheckCauses.operator()<Output>(MemAccess))
            continue;
          const DIMemoryTraitSet &DepTS=MemAccess->getSecond();
          if (SrcInst.mayWriteToMemory() && !SrcInst.mayReadFromMemory()) {
            if (DstInst.mayReadFromMemory() && !DstInst.mayWriteToMemory()) {
              if (MemAccess->is<Flow>())
                AddTrait.operator()<Flow>(ForwTraits, MemAccess);
              if (MemAccess->is<Anti>())
                AddTrait.operator()<Anti>(BackwTraits, MemAccess);
            }
            else
              if (DstInst.mayWriteToMemory() && !DstInst.mayReadFromMemory() &&
                  MemAccess->is<Output>()) {
                AddTrait.operator()<Output>(ForwTraits, MemAccess);
                AddTrait.operator()<Output>(BackwTraits, MemAccess);
              }
          }
          else
            if (SrcInst.mayReadFromMemory() && !SrcInst.mayWriteToMemory() &&
                DstInst.mayWriteToMemory() && !DstInst.mayReadFromMemory()) {
              if (MemAccess->is<Flow>())
                AddTrait.operator()<Flow>(BackwTraits, MemAccess);
              if (MemAccess->is<Anti>())
                AddTrait.operator()<Anti>(ForwTraits, MemAccess);
            }
        }
    }
    NewForwardDep.clear();
    NewBackwardDep.clear();
    for (auto &DepTrait : ForwTraits)
      NewForwardDep.push_back(DepTrait);
    for (auto &DepTrait : BackwTraits)
      NewBackwardDep.push_back(DepTrait);
    return FoundDep || !FoundTrait;
  };
  if (!mDIMInfo.isValid())
    return true;
  if (FoundDINode[Server])
    return FindDIDependencies(SrcDIMems[Server], DstDIMems[Server],
        mServerDIATRel.value());
  else
    if (FoundDINode[Client])
      return FindDIDependencies(SrcDIMems[Client], DstDIMems[Client],
          mClientDIATRel);
    else
      return true;
}

char ProgramDependencyGraphPass::ID=0;

INITIALIZE_PASS_BEGIN(ProgramDependencyGraphPass, "pdg",
  "Program Dependency Graph", false, true)
INITIALIZE_PASS_DEPENDENCY(DependenceAnalysisWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(EstimateMemoryPass)
INITIALIZE_PASS_DEPENDENCY(DIEstimateMemoryPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(ProgramDependencyGraphPass, "pdg",
  "Program Dependency Graph", false, true)

FunctionPass *createProgramDependencyGraphPass() {
  return new ProgramDependencyGraphPass;
}

void ProgramDependencyGraphPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<DependenceAnalysisWrapperPass>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired<EstimateMemoryPass>();
  AU.addRequired<DIEstimateMemoryPass>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
  AU.setPreservesAll();
}

bool ProgramDependencyGraphPass::runOnFunction(Function &F) {
  releaseMemory();
  DependenceAnalysisWrapperPass &DIPass=
      getAnalysis<DependenceAnalysisWrapperPass>();
  LoopInfoWrapperPass &LIPass=getAnalysis<LoopInfoWrapperPass>();
  EstimateMemoryPass &EMPass=getAnalysis<EstimateMemoryPass>();
  DIEstimateMemoryPass &DIEMPass=getAnalysis<DIEstimateMemoryPass>();
  TargetLibraryInfoWrapperPass &TLIPass=
      getAnalysis<TargetLibraryInfoWrapperPass>();
  DIMemoryClientServerInfo DIMInfo(const_cast<DIAliasTree&>(DIEMPass.
      getAliasTree()), *this, F);
  mPDG=new PDG(F);
  mPDGBuilder=new PDGBuilder(*mPDG,
    F,
    DIPass.getDI(),
    EMPass.getAliasTree(),
    DIMInfo,
    TLIPass.getTLI(F),
    LIPass.getLoopInfo(),
    true);
  mPDGBuilder->populate();
  return false;
}

namespace {
struct PDGPassGraphTraits {
  static const PDG *getGraph(ProgramDependencyGraphPass *P) {
    return &P->getPDG();
  }
};

struct PDGPrinter : public DOTGraphTraitsPrinterWrapperPass<
    ProgramDependencyGraphPass, false, const PDG*, PDGPassGraphTraits> {
  static char ID;
  PDGPrinter() : DOTGraphTraitsPrinterWrapperPass<ProgramDependencyGraphPass,
      false, const PDG*, PDGPassGraphTraits>("pdg", ID) {
    initializePDGPrinterPass(*PassRegistry::getPassRegistry());
  }
};
char PDGPrinter::ID=0;

struct PDGViewer : public DOTGraphTraitsViewerWrapperPass<
    ProgramDependencyGraphPass, false, const PDG*,
  PDGPassGraphTraits> {
  static char ID;
  PDGViewer() : DOTGraphTraitsViewerWrapperPass<ProgramDependencyGraphPass,
      false, const PDG*, PDGPassGraphTraits>("pdg", ID) {
    initializePDGViewerPass(*PassRegistry::getPassRegistry());
  }
};
char PDGViewer::ID=0;
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
