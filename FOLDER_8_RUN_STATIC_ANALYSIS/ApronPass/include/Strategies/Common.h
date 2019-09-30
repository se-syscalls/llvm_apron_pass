#ifndef CHAOTIC_EXECUTION_STRATEGIES_COMMON_H
#define CHAOTIC_EXECUTION_STRATEGIES_COMMON_H

struct VertexProperty {
	BasicBlock * basicBlock;
	virtual void vertexCreate(BasicBlock * a_basicBlock) {
		basicBlock = a_basicBlock;
	}
};

class EdgeProperty;

typedef boost::adjacency_list < boost::listS, boost::listS, boost::bidirectionalS, VertexProperty, EdgeProperty> Graph;
typedef Graph::vertex_descriptor Vertex;
typedef Graph::edge_descriptor Edge;
typedef std::map<Vertex, int> VertexIndexMap;
typedef std::map<Vertex, boost::default_color_type> VertexColourMap;

template <typename Graph>
virtual Graph buildGraph() {
	Graph g;
	BasicBlockManager & factory = BasicBlockManager::getInstance();
	Function * function = callGraph().m_function;
	llvm::Function * llvmFunction = function->getLLVMFunction();
	std::map<BasicBlock*, Graph::vertex_descriptor> bbVertexMap;
	for (llvm::BasicBlock & llvmbb : *llvmFunction) {
		BasicBlock * bb = factory.getBasicBlock(&llvmbb);
		Graph::vertex_descriptor v = boost::add_vertex(g);
		g[v].vertexCreate(bb);
		bbVertexMap[bb] = v;
	}
	
	for (llvm::BasicBlock & llvmbb : *llvmFunction) {
		BasicBlock * bb = factory.getBasicBlock(&llvmbb);
		const llvm::TerminatorInst *TInst = llvmbb.getTerminator();
		int NSucc = TInst->getNumSuccessors();
		for (unsigned succIdx = 0; succIdx < NSucc; ++succIdx) {
			llvm::BasicBlock * succ = TInst->getSuccessor(succIdx);
			BasicBlock * succbb = factory.getBasicBlock(succ);
			Graph::vertex_descriptor v0 = bbVertexMap[bb];
			Graph::vertex_descriptor v1 = bbVertexMap[succbb];
			Graph::edge_descriptor e = boost::add_edge(v0, v1, g).first;
			g[e].edgeCreate();
		}
	}
	return g;
}

virtual void initializeRootAbstractState(Graph & g) {
	Graph::vertex_descriptor rootv = *boost::vertices(g).first;
	BasicBlock * root = g[rootv].basicBlock;
	std::vector<std::string> userPointers = root->getFunction()->getUserPointers();
	AbstractState state(userPointers);
	root->getAbstractState() = state;
}

#endif // CHAOTIC_EXECUTION_STRATEGIES_COMMON_H
