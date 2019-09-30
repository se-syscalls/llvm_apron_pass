#include <CallGraph.h>

#include <iostream>
#include <set>

#include <llvm/Analysis/Dominators.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/Support/raw_ostream.h>

CallGraph::CallGraph(Function * function) : m_function(function), m_root(function->getRoot()), m_name(function->getName()), domTree(new llvm::DominatorTree()) {
	constructGraph();
	domTree->runOnFunction(*function->getLLVMFunction());
}

CallGraph::CallGraph(const std::string & name, BasicBlock * root) : m_function(0), m_root(root), m_name(name) {
	constructGraph();
}

void CallGraph::constructGraph() {
	BasicBlockManager & factory = BasicBlockManager::getInstance();
	std::list<llvm::BasicBlock *> worklist;
	std::set<llvm::BasicBlock *> seen;
	worklist.push_back(m_root->getLLVMBasicBlock());
	while (!worklist.empty()) {
		llvm::BasicBlock * bb = worklist.front();
		BasicBlock * bb1 = factory.getBasicBlock(bb);
		worklist.pop_front();
		const llvm::TerminatorInst *TInst = bb->getTerminator();
		int NSucc = TInst->getNumSuccessors();
		for (unsigned succIdx = 0; succIdx < NSucc; ++succIdx) {
			llvm::BasicBlock * succ = TInst->getSuccessor(succIdx);
			BasicBlock * succBasicBlock = factory.getBasicBlock(succ);
			m_nexts.insert(std::pair<BasicBlock*,BasicBlock*>(
					bb1, succBasicBlock));
			m_prevs.insert(std::pair<BasicBlock*,BasicBlock*>(
					succBasicBlock, bb1));
			if (seen.find(succ) == seen.end()) {
				worklist.push_back(succ);
				seen.insert(succ);
			}
		}
	}
}

BasicBlock * CallGraph::getRoot() {
	return m_root;
}

const std::string & CallGraph::getName() const {
	return m_name;
}

std::vector<BasicBlock*> CallGraph::elements(std::multimap<BasicBlock *, BasicBlock *> & source, BasicBlock * block) {
	std::vector<BasicBlock*> result;
	for (auto it = source.lower_bound(block), ie = source.upper_bound(block);
			it != ie; it++) {
		result.push_back(it->second);
	}
	return result;
}

std::vector<BasicBlock*> CallGraph::successors(BasicBlock * block) {
	return elements(m_nexts, block);
}

std::vector<BasicBlock*> CallGraph::predecessory(BasicBlock * block) {
	return elements(m_prevs, block);
}

void CallGraph::printAsDot() {
	std::multimap<BasicBlock *, BasicBlock *>::iterator it;
	llvm::errs() << "digraph \"" << getName() << "\" {" << "\n";
	for (it = m_nexts.begin(); it != m_nexts.end(); it++) {
		llvm::errs() << "\t"
				<< "\"" << it->first->getName() << "\""
				<< " -> "
				<< "\"" << it->second->getName() << "\"";
		if (isDominates(it->second, it->first)) {
			llvm::errs() << " [color=red]";
		}
		llvm::errs() << "\n";
	}
	llvm::errs() << "}" << "\n";
}

bool CallGraph::isDominates(BasicBlock * dominator, BasicBlock * dominated) const {
	llvm::BasicBlock * llvmDominator = dominator->getLLVMBasicBlock();
	llvm::BasicBlock * llvmDominated = dominated->getLLVMBasicBlock();
	return domTree->dominates(llvmDominator, llvmDominated);
}

bool CallGraph::isStrictDominates(BasicBlock * dominator, BasicBlock * dominated) const {
	llvm::BasicBlock * llvmDominator = dominator->getLLVMBasicBlock();
	llvm::BasicBlock * llvmDominated = dominated->getLLVMBasicBlock();
	return domTree->properlyDominates(llvmDominator, llvmDominated);
}

