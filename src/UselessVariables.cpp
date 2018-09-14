//=== UselessVariables.cpp - High Level Variables Loop Analyzer --------*- C++ -*-===//
//
//                       Traits Static Analyzer (SAPFOR)
//
//===----------------------------------------------------------------------===//
//
// This file implements classes to find declarations of uselsess variables 
// in a source code.
//
//===----------------------------------------------------------------------===//



#include "DFRegionInfo.h"
#include "UselessVariables.h"
#include <clang/AST/Decl.h>
#include <clang/AST/Stmt.h>

#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Stmt.h>
#include <llvm/ADT/Statistic.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/Module.h>


#include "tsar_transformation.h"
#include <clang/Rewrite/Core/Rewriter.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/Support/Casting.h>

#include <iostream>
#include <assert.h>
#include <typeinfo>
#include <vector>


using namespace llvm;
using namespace clang;



char ClangUselessVariablesPass::ID = 0;

INITIALIZE_PASS_BEGIN(ClangUselessVariablesPass, "useless-vars", "Searching of useless vars", true, true)
INITIALIZE_PASS_DEPENDENCY(TransformationEnginePass)
INITIALIZE_PASS_END(ClangUselessVariablesPass, "useless-vars", "Searching of useless vars", true, true)

namespace {
/// This visits and analyzes all for-loops in a source code.
class DeclVisitor : public RecursiveASTVisitor<DeclVisitor> {
public:
  /// Creates visitor.
  explicit DeclVisitor(){}

  bool VisitVarDecl(VarDecl *D)
  {
    pDecls_.insert(D);
    return true;    
  }

  bool VisitDeclRefExpr(DeclRefExpr *D)
  {
    //сделать нормальное приведение типов
    pDecls_.erase(   (VarDecl*)(D->getFoundDecl())   );
    return true;
  }

  void DelVarsFromCode(clang::Rewriter &mRewriter)
  {
    for(auto i = pDecls_.begin(); i != pDecls_.end(); i++)
    {
      std::cout << "Del decl:  " << (*i)->getNameAsString() << std::endl;

      std::cout << "check decl :  " << (*i)->isLocalVarDecl() << std::endl;
      std::cout << "check param :  " << (*i)->isLocalVarDeclOrParm() << std::endl;


      //добавить проерку единичной инициализации через traversestmt
      //попробовать проверку через location

      //добавить проверку таких ситуаций
      //if(int x = 0)

      
    }


    //for(auto i = pDecls_.begin(); i != pDecls_.end(); i++)
    //  mRewriter.RemoveText((*i)->getSourceRange());
    


    //after using this function clang delete declarations
    // but add some strange pragmas 
    mRewriter.overwriteChangedFiles();

  }



  void print_decls()
  {
    for(auto i = pDecls_.begin(); i != pDecls_.end(); i++)
    {
      std::cout << "DenseSet:  " << (*i)->getNameAsString() << std::endl;
    }

  }


private:
  DenseSet<VarDecl*>pDecls_;
};
}


bool ClangUselessVariablesPass::runOnFunction(Function &F) {
  std::cout << std::endl << std::endl << "=======function pass========" << std::endl;
 
  auto M = F.getParent();
  auto TfmCtx = getAnalysis<TransformationEnginePass>().getContext(*M);
  if (!TfmCtx || !TfmCtx->hasInstance())
    return false;
  auto FuncDecl = TfmCtx->getDeclForMangledName(F.getName());
  if (!FuncDecl)
    return false;

  DeclVisitor Visitor;
  Visitor.TraverseDecl(FuncDecl);

  Visitor.TraverseStmt(FuncDecl);


  //Visitor.print_decls();

  std::cout << "######### usless Useless Variables" << std::endl;
  Visitor.print_decls();

  

  Visitor.DelVarsFromCode(TfmCtx->getRewriter());
  return false;
}

void ClangUselessVariablesPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TransformationEnginePass>();
  AU.setPreservesAll();
}

FunctionPass *llvm::createClangUselessVariablesPass() {
  return new ClangUselessVariablesPass();
}
