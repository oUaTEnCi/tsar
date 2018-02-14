//===---- tsar_pass.h --------- Create TSAR Passes --------------*- C++ -*-===//
//
//                       Traits Static Analyzer (SAPFOR)
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions that is necessary to combine TSAR and LLVM.
// It contains declarations definitions of functions that initialize and
// create an instances of TSAR passes.
//
//===----------------------------------------------------------------------===//

#ifndef TSAR_PASS_H
#define TSAR_PASS_H

#include <llvm/ADT/StringRef.h>

namespace bcl {
class IntrusiveConnection;
class RedirectIO;
}

namespace tsar {
struct ASTImportInfo;
}

namespace llvm {
class Pass;
class PassInfo;
class PassRegistry;
class FunctionPass;
class ModulePass;
class ImmutablePass;
class raw_ostream;

/// Initializes all passes developed for TSAR project
void initializeTSAR(PassRegistry &Registry);

/// Initializes a pass to builde hierarchy of data-flow regions.
void initializeDFRegionInfoPassPass(PassRegistry &Registry);

/// Creates a pass to builde hierarchy of data-flow regions.
FunctionPass * createDFRegionInfoPass();

/// Initializes a pass to find must defined locations for each data-flow region.
void initializeDefinedMemoryPassPass(PassRegistry &Registry);

/// Creates a pass to find must defined locations for each data-flow region.
FunctionPass * createDefinedMemoryPass();

/// Initializes a pass to find live locations for each data-flow region.
void initializeLiveMemoryPassPass(PassRegistry &Registry);

/// Creates a pass to find live locations for each data-flow region.
FunctionPass * createLiveMemoryPass();

/// Initializes a pass to build hierarchy of accessed memory.
void initializeEstimateMemoryPassPass(PassRegistry &Registry);

/// Creates a pass to build hierarchy of accessed memory.
FunctionPass * createEstimateMemoryPass();

/// Initializes a pass to build hierarchy of accessed memory.
void initializeDIEstimateMemoryPassPass(PassRegistry &Registry);

/// Creates a pass to build hierarchy of accessed memory.
FunctionPass * createDIEstimateMemoryPass();

/// Initializes storage of alias trees.
void initializeDIAliasTreeImmutableStoragePass(PassRegistry &Registry);
/// Initializes a pass to display alias tree.
void initializeAliasTreeViewerPass(PassRegistry &Registry);

/// Creates a pass to display alias tree.
FunctionPass * createAliasTreeViewerPass();

/// Initializes a pass to display alias tree (alias summary only).
void initializeAliasTreeOnlyViewerPass(PassRegistry &Registry);

/// Creates a pass to display alias tree (alias summary only).
FunctionPass * createAliasTreeOnlyViewerPass();

/// Initializes a pass to display alias tree.
void initializeDIAliasTreeViewerPass(PassRegistry &Registry);

/// Creates a pass to display alias tree.
FunctionPass * createDIAliasTreeViewerPass();

/// Initializes a pass to print alias tree to 'dot' file.
void initializeAliasTreePrinterPass(PassRegistry &Registry);

/// Creates a pass to print alias tree to 'dot' file.
FunctionPass * createAliasTreePrinterPass();

/// Initializes a pass to print alias tree to 'dot' file (alias summary only).
void initializeAliasTreeOnlyPrinterPass(PassRegistry &Registry);

/// Creates a pass to print alias tree to 'dot' file (alias summary only).
FunctionPass * createAliasTreeOnlyPrinterPass();

/// Initializes a pass to print alias tree to 'dot' file.
void initializeDIAliasTreePrinterPass(PassRegistry &Registry);

/// Creates a pass to print alias tree to 'dot' file.
FunctionPass * createDIAliasTreePrinterPass();

/// Initializes a pass to analyze private variables.
void initializePrivateRecognitionPassPass(PassRegistry &Registry);

/// Creates a pass to analyze private variables.
FunctionPass * createPrivateRecognitionPass();

/// Initializes a pass to fetch private variables before they will be promoted
/// to registers or removed.
void initializeFetchPromotePrivatePassPass(PassRegistry &Registry);

/// Creates an interaction pass to obtain results of private variables analysis.
ModulePass * createPrivateServerPass(
  bcl::IntrusiveConnection &IC, bcl::RedirectIO &StdErr);

/// Initializes an interaction pass to obtain results of private variables
/// analysis.
void initializePrivateServerPassPass(PassRegistry &Registry);

/// Creates a pass to fetch private variables before they will be promoted
/// to registers or removed.
FunctionPass * createFetchPromotePrivatePass();

/// Initializes a pass to access source level transformation enginer.
void initializeTransformationEnginePassPass(PassRegistry &Registry);

/// Creates a pass to make more precise analysis of for-loops in C sources.
FunctionPass * createPrivateCClassifierPass();

/// Initializes a pass to make more precise analysis of for-loops in C sources.
void initializePrivateCClassifierPassPass(PassRegistry &Registry);

/// Creates a pass to access source level transformation enginer.
ImmutablePass * createTransformationEnginePass();

/// Initializes a pass to perform low-level (LLVM IR) instrumentation of program.
void initializeInstrumentationPassPass(PassRegistry &Registry);

/// Creates a pass to perform low-level (LLVM IR) instrumentation of program.
ModulePass * createInstrumentationPass();

/// Initialize a pass to match high-level and low-level loops.
void initializeLoopMatcherPassPass(PassRegistry &Registry);

/// Creates a pass to match high-level and low-level loops.
FunctionPass * createLoopMatcherPass();

/// Initializes a pass to match variables and allocas (or global variables).
void initializeMemoryMatcherPassPass(PassRegistry &Registry);

/// Initializes a pass to match variables and allocas (or global variables).
void initializeMemoryMatcherImmutableStoragePass(PassRegistry &Registry);

/// Initializes a pass to match variables and allocas (or global variables).
void initializeMemoryMatcherImmutableWrapperPass(PassRegistry &Registry);

/// Creates a pass to match variables and allocas (or global variables).
ModulePass * createMemoryMatcherPass();

/// Creates a pass to print internal state of the specified pass after the
/// last execution.
FunctionPass * createFunctionPassPrinter(const PassInfo *PI, raw_ostream &OS);

/// Initializes a pass to print results of a test.
void initializeTestPrinterPassPass(PassRegistry &Registry);

/// Creates a pass to print results of a test.
ModulePass * createTestPrinterPass();

/// Initializes a pass which retrieves some debug information for a loop if
/// it is not presented in LLVM IR.
void initializeDILoopRetrieverPassPass(PassRegistry &Registry);

/// Creates a pass which retrieves some debug information for a loop if
/// it is not presented in LLVM IR.
Pass * createDILoopRetrieverPass();

/// Initializes a pass to determine perfect for-loops in a source code.
void initializeClangPerfectLoopPassPass(PassRegistry &Registry);

/// Creates a pass to determine perfect for-loops in a source code.
FunctionPass * createClangPerfectLoopPass();

/// Initializes a pass to determine canonical for-loops in a source code.
void initializeCanonicalLoopPassPass(PassRegistry &Registry);

/// Creates a pass to determine canonical for-loops in a source code.
FunctionPass * createCanonicalLoopPass();

/// Initializes a pass to perform source-level inline expansion using Clang.
void initializeClangInlinerPassPass(PassRegistry& Registry);

/// Creates a pass to perform source-level inline expansion using Clang.
llvm::ModulePass *createClangInlinerPass();

/// Initializes a pass to reformat sources after transformation using Clang.
void initializeClangFormatPassPass(PassRegistry& Registry);

/// Creates a pass to reformat sources after transformation using Clang.
llvm::ModulePass *createClangFormatPass();

/// Creates a pass to reformat sources after transformation using Clang.
llvm::ModulePass* createClangFormatPass(
  llvm::StringRef OutputSuffix, bool NoFormat);

/// Initializes pass to check absence of a macro in source ranges which
/// are marked with `assert nomacro` directive.
void initializeClangNoMacroAssertPass(PassRegistry& Registry);

/// Initializes pass to check absence of a macro in source ranges which
/// are marked with `assert nomacro` directive.
llvm::FunctionPass * createClangNoMacroAssert(bool *IsInvalid = nullptr);

/// Initializes a pass which collects information about source-level globals.
void initializeClangGlobalInfoPassPass(PassRegistry &Registry);

/// Creates a pass which collects information about source-level globals.
llvm::ModulePass * createClangGlobalInfoPass();

/// Initializes a pass to obtain access to the import process information.
void initializeImmutableASTImportInfoPassPass(PassRegistry &Registry);

/// Creates a pass to obtain access to the import process information.
llvm::ImmutablePass * createImmutableASTImportInfoPass(
  const tsar::ASTImportInfo &Info);

void initializeCopyEliminationPassPass(PassRegistry& Registry);
}

#endif//TSAR_PASS_H
