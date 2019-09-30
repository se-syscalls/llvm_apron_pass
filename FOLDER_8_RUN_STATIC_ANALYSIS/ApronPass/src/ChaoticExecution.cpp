#include <assert.h>
#include <unordered_set>
#include <list>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/depth_first_search.hpp>
#include <boost/graph/topological_sort.hpp>
#include <llvm/IR/Function.h>

#include <Value.h>
#include <BasicBlock.h>
#include <CallGraph.h>
#include <ChaoticExecution.h>
#include <Function.h>

extern bool Debug;
extern unsigned UpdateCountMax;
extern unsigned WideningThreshold;

class VertexProperty;
class EdgeProperty;

typedef boost::adjacency_list < boost::listS, boost::listS, boost::bidirectionalS, VertexProperty, EdgeProperty> Graph;
//typedef boost::adjacency_list < boost::listS, boost::listS, boost::bidirectionalS, boost::no_property, boost::no_property, GraphProperty> Graph;
typedef Graph::vertex_descriptor Vertex;
typedef Graph::edge_descriptor Edge;
typedef std::map<Vertex, int> VertexIndexMap;
typedef std::map<Vertex, boost::default_color_type> VertexColourMap;

template <typename Graph>
void printToDot(Graph & g);
void analyze(Graph & g, Function * function);

struct VertexProperty {
	Graph * region = 0;
	Vertex vertex = 0;
	std::string name;
	mutable BasicBlock * basicBlock = 0;
	mutable AbstractState abstractState;
	mutable int joinCount = 0;
	mutable bool isDirty = true;
};

struct EdgeProperty {
	// If the source vertex is simple, use the source vertex.
	// If the source vertex is a region, use the source vertex from with
	// the region - i.e. the original source vertex for the edge
	Graph * sourceRegion = 0;
	Vertex sourceVertex = 0;
	// If the target vertex is simple, use the target vertex.
	// If the target vertex is a region, use the region's head
	Graph * targetRegion = 0;
	Vertex targetVertex = 0;
};

template <typename T>
struct BGLIterableClass {
	T first;
	T last;
	BGLIterableClass(std::pair<T,T> bgl_iterator) : first(bgl_iterator.first),
			last(bgl_iterator.second) {}
	T begin() {return first; }
	T end() {return last; }
};

template <typename T>
BGLIterableClass<T> BGLIterable(std::pair<T,T> bgl_iterator) {
	return BGLIterableClass<T>(bgl_iterator);
}

class Strategy {
public:
	CallGraph * m_callGraph = 0;
	CallGraph & callGraph() { return *m_callGraph; }
	virtual void execute() = 0;
};

class BFSStrategy : public Strategy {
	virtual void execute() {
		abort();
	}
};

class DFSStrategy : public Strategy {
	virtual void execute() {
		abort();
	}
};

class WTOStrategy : public Strategy {
	virtual void execute() {
		Graph g = buildGraph();
		llvm::errs() << "Original graph:\n";
		printToDot(g);
		initializeRootAbstractState(g);
		reduce(g);
		llvm::errs() << "Reduced graph:\n";
		printToDot(g);
		analyze(g, callGraph().m_function);
		//llvm::errs() << "Analyzed graph:\n";
		//printToDot(g);
	}

	// XXX Move to parent class? To library?
	virtual Graph buildGraph() {
		Graph g;
		BasicBlockManager & factory = BasicBlockManager::getInstance();
		Function * function = callGraph().m_function;
		llvm::Function * llvmFunction = function->getLLVMFunction();
		std::map<BasicBlock*, Vertex> bbVertexMap;
		for (llvm::BasicBlock & llvmbb : *llvmFunction) {
			BasicBlock * bb = factory.getBasicBlock(&llvmbb);
			Vertex v = boost::add_vertex(g);
			g[v].basicBlock = bb;
			g[v].name = bb->getName();
			bbVertexMap[bb] = v;
		}
		
		for (std::pair<BasicBlock *, BasicBlock*> edge : callGraph().m_nexts) {
			Vertex v0 = bbVertexMap[edge.first];
			Vertex v1 = bbVertexMap[edge.second];
			boost::add_edge(v0, v1, g);
		}
		return g;
	}

	// XXX Move to parent class? To library?
	virtual void initializeRootAbstractState(Graph & g) {
		BasicBlock * root = callGraph().getRoot();
		std::vector<std::string> userPointers = root->getFunction()->getUserPointers();
		AbstractState state(userPointers);
		root->getAbstractState() = state;
	}

	virtual void reduce(Graph & g) {
		while (true) {
			std::list<Edge> backEdges = getBackEdges(g);
			while (!backEdges.empty()) {
				Edge backEdge = backEdges.front();
				backEdges.pop_front();
				Vertex source = boost::source(backEdge, g);
				Vertex target = boost::target(backEdge, g);
				if (Debug) {
					llvm::errs() << "Debug: Reducing backedge: " << g[source].name <<
							" -> " << g[target].name << "\n";
				}
				if (reduceLoop(backEdge, g)) {
					break;
				}
			}
			if (backEdges.empty()) {
				if (Debug) {
					llvm::errs() << "Debug: no more backedges\n";
				}
				return;
			}
		}
	}

	virtual std::list<Edge> getBackEdges(Graph & g) {
		std::list<Edge> backEdges;
		DiscoverLoopsVisitor visitor(callGraph(), backEdges);
		VertexIndexMap indexmap;
		int index = 0;
		for (Vertex v : BGLIterable(boost::vertices(g))) {
			indexmap[v] = index++;
		}
		boost::depth_first_search(g, boost::visitor(visitor).vertex_index_map(boost::associative_property_map<VertexIndexMap>(indexmap)));
		return backEdges;
	}

	struct DiscoverLoopsVisitor : public boost::default_dfs_visitor {
		std::list<Edge> & backEdges;
		CallGraph & callGraph;
		DiscoverLoopsVisitor(CallGraph & callGraph, std::list<Edge> & backEdges) :
				boost::default_dfs_visitor(),
				callGraph(callGraph),
				backEdges(backEdges) {}

		template <typename Edge, typename Graph>
		void verifyHeadDominatesTailForBackEdge(Edge & edge, Graph & g) {
			// Verify head dominates tail:
			Vertex tail = boost::source(edge, g);
			Vertex head = boost::target(edge, g);
			BasicBlock * tailbb = g[tail].basicBlock;
			BasicBlock * headbb = g[head].basicBlock;
			if ((!tailbb) || (!headbb)) {
				// Region. We're good
				return;
			}
			if (!callGraph.isDominates(headbb, tailbb)) {
				llvm::errs() << "Warning: Back edge to non-dominating node: Edge: "
						<< tailbb->getName() << " -> " << headbb->getName() << "\n";
				printToDot(g);
			}
		}

		template <typename Edge, typename Graph>
		void back_edge(Edge edge, Graph & g) {
			backEdges.push_back(edge);
			verifyHeadDominatesTailForBackEdge(edge, g);
		}
	};

	virtual bool reduceLoop(Edge & backEdge, Graph & g) {
		// XXX This function can be improved greatly
		Vertex origtail = boost::source(backEdge, g);
		Vertex orighead = boost::target(backEdge, g);

		// START get loop members
		std::map<Vertex, Vertex> reversegVertexMapping;
		Graph reverseg = reverse(g, reversegVertexMapping);
		Vertex tail = reversegVertexMapping[origtail];
		Vertex head = reversegVertexMapping[orighead];
		boost::clear_out_edges(head, reverseg);
		std::set<Vertex> loopMembersSet;
		LoopMembersVisitor<Graph> visitor(loopMembersSet, head, tail);
		VertexIndexMap indexmap;
		int index = 0;
		for (Vertex v : BGLIterable(boost::vertices(reverseg))) {
			indexmap[v] = index++;
		}
		boost::depth_first_search(reverseg, boost::visitor(visitor).root_vertex(tail).vertex_index_map(boost::associative_property_map<VertexIndexMap>(indexmap)));
		if (loopMembersSet.size() == boost::num_vertices(g)) {
			// Loop is entire region. Reduction doesn't make sense
			return false;
		}
		std::list<Vertex> loopMembers(loopMembersSet.begin(), loopMembersSet.end());
		VertexIndexComparator vertexIndexComparator(indexmap);
		loopMembers.sort(vertexIndexComparator);
		std::list<Vertex> loopMembersOrigGraph;
		for (Vertex member : loopMembers) {
			loopMembersOrigGraph.push_back(reverseg[member].vertex);
		}
		// END get loop members

		std::set<Vertex> revIncomingVertices = getIncomingVertices(reverseg, loopMembersSet);
		std::set<Vertex> targetVertices;
		for (Vertex v : revIncomingVertices) {
			Vertex u = reverseg[v].vertex;
			targetVertices.insert(u);
		}

		std::set<Vertex> sourceVertices;
		for (Edge edge : BGLIterable(boost::in_edges(orighead, g))) {
			sourceVertices.insert(boost::source(edge, g));
		}
		sourceVertices.erase(origtail);

		// Create region
		Graph * graphp = new Graph();
		Graph & region = *graphp;
		std::ostringstream oss;
		oss << "Region " << g[orighead].name << " -> " << g[origtail].name;
		std::string regionName = oss.str();

		std::map<Vertex, Vertex> regionGraphVertexMapping;
		for (Vertex member : loopMembersOrigGraph) {
			Vertex vertex = boost::add_vertex(g[member], region);
			region[vertex].vertex = member;
			regionGraphVertexMapping[member] = vertex;
		}

		for (Vertex member : BGLIterable(boost::vertices(region))) {
			Vertex origMember = region[member].vertex;
			if (origMember == origtail) {
				continue;
			}
			for (Edge e : BGLIterable(boost::out_edges(origMember, g))) {
				Vertex origTarget = boost::target(e, g);
				auto targetit = regionGraphVertexMapping.find(origTarget);
				if (targetit == regionGraphVertexMapping.end()) {
					continue; // Edge to outside of region
				}
				Vertex target = targetit->second;
				boost::add_edge(member, target, g[e], region);
			}
		}
		Vertex regionHead = regionGraphVertexMapping[orighead];
		Vertex regionTail = regionGraphVertexMapping[origtail];
		boost::add_edge(regionTail, regionHead, region);

		// Modification to g starts here!
		Vertex regionVertex = boost::add_vertex(g);
		g[regionVertex].region = graphp;
		g[regionVertex].name = regionName;

		std::set<Vertex> loopMembersOrigGraphSet(loopMembersOrigGraph.begin(), loopMembersOrigGraph.end());
		for (Vertex member : loopMembersOrigGraph) {
			for (Edge e : BGLIterable(boost::out_edges(member, g))) {
				Vertex target = boost::target(e, g);
				if (loopMembersOrigGraphSet.count(target) != 0) {
					continue;
				}
				std::pair<Edge, bool> newedgepair = boost::add_edge(regionVertex, target, g[e], g);
				Edge & newedge = newedgepair.first;
				if (!g[newedge].sourceRegion) {
					g[newedge].sourceRegion = &region;
					g[newedge].sourceVertex = regionGraphVertexMapping[member];
				}
			}
		}

		for (Edge edge : BGLIterable(boost::in_edges(orighead, g))) {
			Vertex source = boost::source(edge, g);
			Vertex target = boost::target(edge, g);
			if (source == origtail) {
				continue;
			}
			std::pair<Edge, bool> newedgepair = boost::add_edge(source, regionVertex, g[edge], g);
			Edge newedge = newedgepair.first;
			if (!g[newedge].targetRegion) {
				g[newedge].targetRegion = &region;
				g[newedge].targetVertex = regionHead;
			}
		}

		for (Vertex member : loopMembersOrigGraph) {
		        boost::clear_vertex(member, g);
			boost::remove_vertex(member, g);
		}

		reduce(region);
		return true;
	}

	struct VertexIndexComparator {
		VertexIndexMap & indexmap;
		VertexIndexComparator(VertexIndexMap & indexmap) : indexmap(indexmap) {}
		bool operator()(Vertex & v, Vertex & u) {
			return (indexmap[v] < indexmap[u]);
		}
	};

	std::set<Vertex> getIncomingVertices(Graph & g, std::set<Vertex> & loopMembers) {
		std::set<Vertex> result;
		for (Vertex v : loopMembers) {
			for (Vertex u : BGLIterable(boost::inv_adjacent_vertices(v, g))) {
				if (loopMembers.count(u) == 0) {
					result.insert(u);
				}
			}
		}
		return result;
	}

	std::set<Vertex> getOutgoingVertices(Graph & g, std::set<Vertex> & loopMembers) {
		std::set<Vertex> result;
		for (Vertex v : loopMembers) {
			for (Vertex u : BGLIterable(boost::adjacent_vertices(v, g))) {
				if (loopMembers.count(u) == 0) {
					result.insert(u);
				}
			}
		}
		return result;
	}

	template<typename Graph>
	struct LoopMembersVisitor : boost::default_dfs_visitor {
		typedef typename Graph::vertex_descriptor Vertex;
		typedef typename Graph::edge_descriptor Edge;

		std::set<Vertex> & loopMembers;
		Vertex tail;
		Vertex head;
		bool inLoop = true;
		bool finished = false;
		LoopMembersVisitor(std::set<Vertex> & loopMembers, Vertex head, Vertex tail) : loopMembers(loopMembers), head(head), tail(tail) {}

		template <typename Vertex, typename Graph_>
		void start_vertex(Vertex v, Graph_ & g) {
			if (finished) {
				return;
			}
			if (v != tail) {
				llvm::errs() << "Warning: LoopMembersVisitor::start_vertex with non-tail: " << g[v].name << "\n";
			}
		}

		template <typename Vertex, typename Graph_>
		void discover_vertex(Vertex v, Graph_ & g) {
			if (finished) {
				return;
			}
			if (inLoop) {
				loopMembers.insert(v);
			}
			if (v == head) {
				inLoop = false;
			}
		}

		template <typename Vertex, typename Graph_>
		void finish_vertex(Vertex v, Graph_ & g) {
			if (finished) {
				return;
			}
			if (v == tail) {
				finished = true;
			}
			if (inLoop) {
				return;
			}
			if (v == head) {
				inLoop = true;
			}
		}
	};

	virtual Graph reverse(Graph & orig, std::map<Vertex, Vertex> & mapping) {
		Graph g;
		for (Vertex v : BGLIterable(boost::vertices(orig))) {
			Vertex newv = boost::add_vertex(g);
			g[newv] = orig[v];
			g[newv].vertex = v;
			mapping[v] = newv;
		}

		std::list<std::pair<Vertex, Vertex> > edges;
		for (Edge edge : BGLIterable(boost::edges(orig))) {
			Vertex origsrc = boost::source(edge, orig);
			Vertex source = mapping[origsrc];
			if (source == 0) {
				VertexProperty & vprops = orig[origsrc];
				llvm::errs() << "Failed to find reverse mapping for: " << vprops.name << "\n";
				abort();
			}
			Vertex origtgt = boost::target(edge, orig);
			Vertex target = mapping[origtgt];
			if (target == 0) {
				VertexProperty & vprops = orig[origtgt];
				llvm::errs() << "Failed to find reverse mapping for: " << vprops.name << "\n";
				abort();
			}
			edges.push_back(std::make_pair(target, source));
		}
		for (std::pair<Vertex, Vertex> & edge : edges) {
			boost::add_edge(edge.first, edge.second, g);
		}
		return g;
	}

};

struct WTOAnalysis {

void analyze(Graph & g, Function * function) {
	std::list<Vertex> topologicalOrder = getTopologicalOrder(g);
	VertexIndexMap indexmap;
	std::map<Vertex, std::set<Value*> > lifetimeValues;
	int index = 0;
	for (Vertex v : topologicalOrder) {
		indexmap[v] = index++;
		g[v].isDirty = true;
	}

	populate_lifetime_values(lifetimeValues, g, topologicalOrder);

	while (g[*topologicalOrder.begin()].isDirty) {
		for (Vertex v : topologicalOrder) {
			if (!g[v].isDirty) {
				continue;
			}
			g[v].isDirty = false;

			analyze_vertex(v, g, function);
			remove_lifetime_ended_values(lifetimeValues, v, g, function);
			for (Edge e : BGLIterable(boost::out_edges(v, g))) {
				Vertex source = boost::source(e, g);
				Vertex target = boost::target(e, g);
				bool is_widen = false;
				if (indexmap[source] >= indexmap[target]) {
					// back edge
					int & joinCount = g[target].joinCount;
					++joinCount;
					is_widen = (joinCount >= WideningThreshold);
				}
				join_or_widen_over_edge(e, g, is_widen);
			}
		}
	}
}

void populate_lifetime_values(
		std::map<Vertex, std::set<Value*> > & lifetimeValues, Graph & g, std::list<Vertex> & topologicalOrder) {
	std::set<Value * > values;

	Vertex first = *topologicalOrder.begin();
	VertexProperty & vprop = g[first];
	BasicBlock * bb = vprop.basicBlock;
	assert(bb);
	std::list<Value *> phivalues = bb->phivalues();
	for (Value * phivalue : phivalues) {
		phivalue->popLifetimeValues(values);
	}

	for (auto it = topologicalOrder.rbegin(), ie = topologicalOrder.rend(); 
			it != ie; it++) {
		Vertex v = *it;
		populate_lifetime_values_vertex(values, v, g);
		lifetimeValues[v] = values;
	}
}

void populate_lifetime_values_vertex(std::set<Value*> & lifetimeValues,
		Vertex v, Graph & g) {
	VertexProperty & vprop = g[v];
	BasicBlock * bb = vprop.basicBlock;
	if (bb) {
		std::list<Value*> values = bb->values();
		for (Value * value : values) {
			value->popLifetimeValues(lifetimeValues);
		}
		return;
	}
	Graph * region = vprop.region;
	if (region) {
		for (Vertex v1 : BGLIterable(boost::vertices(*region))) {
			populate_lifetime_values_vertex(lifetimeValues, v1, *region);
		}
		return;
	}
	llvm::errs() << "Bad vertex: " << vprop.name << "\n";
	abort();
}

template <typename OutputIterator>
struct topo_sort_visitor_allow_back_edges : boost::topo_sort_visitor<OutputIterator> {
	topo_sort_visitor_allow_back_edges(OutputIterator & oit) : boost::topo_sort_visitor<OutputIterator>(oit) {}

    template <typename Edge, typename Graph>
    void back_edge(const Edge&, Graph&) { /* Ignore */ }
};

template <typename OutputIterator>
topo_sort_visitor_allow_back_edges<OutputIterator> getVisitor(OutputIterator & oit) {
	return topo_sort_visitor_allow_back_edges<OutputIterator>(oit);
}

std::list<Vertex> getTopologicalOrder(Graph & g) {
	std::list<Vertex> topologicalOrder;
	VertexColourMap colourmap;
	auto inserter = std::front_inserter(topologicalOrder);
	auto visitor = getVisitor(inserter);
	boost::depth_first_search(g, boost::visitor(visitor).color_map(boost::associative_property_map<VertexColourMap>(colourmap)));
	return topologicalOrder;
}

template <typename Vertex, typename Graph_>
void analyze_vertex(Vertex v, Graph_ & g, Function * function) {
	if (g[v].basicBlock) {
		llvm::errs() << "Debug: StaticAnalysisVisitor::discover_vertex start simple: " << g[v].name << "\n";
		discover_simple_verex(v, g, g[v].basicBlock);
		llvm::errs() << "Debug: StaticAnalysisVisitor::discover_vertex done simple: " << g[v].name << "\n";
	} else if (g[v].region) {
		llvm::errs() << "Debug: StaticAnalysisVisitor::discover_vertex: start region: " << g[v].name << "\n";
		discover_region_verex(v, g, *g[v].region, function);
		llvm::errs() << "Debug: StaticAnalysisVisitor::discover_vertex: done region: " << g[v].name << "\n";
	} else {
		llvm::errs() << "Unknown tree vertex that is neither simple nor region: " << g[v].name << "\n";
	}
}

template <typename Vertex, typename Graph_>
void discover_simple_verex(Vertex v, Graph_ & g, BasicBlock * block) {
	g[v].abstractState = block->getAbstractState();
	block->update(g[v].abstractState);
}

template <typename Vertex, typename Graph_>
void discover_region_verex(Vertex v, Graph_ & g, Graph & region, Function * function) {
	analyze(region, function);
}

template <typename Edge, typename Graph_>
void join_or_widen_over_edge(Edge e, Graph_ & g, bool is_widen=false) {
	BasicBlock * source = getSourceBasicBlock(e, g);
	AbstractState & state = getSourceAbstractState(e, g);
	BasicBlock * target = getTargetBasicBlock(e, g);
	llvm::errs() << "Debug: StaticAnalysisVisitor::join_or_widen_basic_blocks enter: " << source->getName() << " -> " << target->getName() << "\n";
	llvm::errs() << "\t\top: " << (is_widen ? "Widen" : "Join") << "\n";
	bool isChanged = join_or_widen_basic_blocks(source, target, state, is_widen);
	if (isChanged) {
		mark_for_revisit(boost::target(e, g), g);
	}
	llvm::errs() << "Debug: StaticAnalysisVisitor::join_or_widen_basic_blocks done: " << source->getName() << " -> " << target->getName() << "\n";
	llvm::errs() << "\t\top: " << (is_widen ? "Widen" : "Join") <<
			" changed: " << isChanged << "\n";
}

void remove_lifetime_ended_values(std::map<Vertex, std::set<Value*> > & lifetimeValues,
		Vertex v, Graph & g, Function * function) {
	std::set<Value*> & vLifetimeVals = lifetimeValues[v];
	const VertexProperty & vprops = g[v];
	if (vprops.region) {
		return;
	}
	AbstractState & abstractState = vprops.abstractState;
	ApronAbstractState & aas = abstractState.m_apronAbstractState;
	for (std::string varname : ApronAbstractState::Variables(aas)) {
		if (isForgetVarname(varname, vLifetimeVals, function)) {
			llvm::errs() << "Debug: remove_lifetime_ended_values: removing: " << varname << "\n";
			aas.forget(varname);
		}
	}
	// XXX In theory, we should also minimize
}

bool isForgetVarname(std::string & varname, std::set<Value*> & vLifetimeVals, Function * function) {
	if (function->isVarInOut(varname.c_str())) {
		return false;
	}
	if (function->isOffsetVariable(varname.c_str())) {
		return false;
	}
	for (Value * value : vLifetimeVals) {
		if (varname == value->getName()) {
			return false;
		}
	}
	return true;
}

template <typename Vertex, typename Graph_>
void mark_for_revisit(Vertex v, Graph_ &g) {
	g[v].isDirty = true;
}

template <typename Edge, typename Graph_>
BasicBlock * getSourceBasicBlock(Edge e, Graph_ & g) {
	const VertexProperty & vprops = getSourceVertexProperty(e, g);
	assert(vprops.basicBlock);
	return vprops.basicBlock;
}

template <typename Edge, typename Graph_>
BasicBlock * getTargetBasicBlock(Edge e, Graph_ & g) {
	const VertexProperty & vprops = getTargetVertexProperty(e, g);
	assert(vprops.basicBlock);
	return vprops.basicBlock;
}

template <typename Edge, typename Graph_>
AbstractState & getSourceAbstractState(Edge e, Graph_ & g) {
	const VertexProperty & vprops = getSourceVertexProperty(e, g);
	return vprops.abstractState;
}

template <typename Edge, typename Graph_>
const VertexProperty & getTargetVertexProperty(Edge e, Graph_ & g) {
	const EdgeProperty & eprop = g[e];
	if (eprop.targetRegion) {
		const Graph & region = *eprop.targetRegion;
		return region[eprop.targetVertex];
	}
	return g[boost::target(e, g)];
}

template <typename Edge, typename Graph_>
const VertexProperty & getSourceVertexProperty(Edge e, Graph_ & g) {
	const EdgeProperty & eprop = g[e];
	if (eprop.sourceRegion) {
		const Graph & region = *eprop.sourceRegion;
		return region[eprop.sourceVertex];
	}
	return g[boost::source(e, g)];
}

bool join_or_widen_basic_blocks(BasicBlock * source, BasicBlock * dest, AbstractState & state, bool is_widen=false) {
	AbstractState incoming = dest->getAbstractStateWithAssumptions(*source, state);
	bool isChanged;
	if (is_widen) {
		isChanged = dest->getAbstractState().widen(incoming);
	} else {
		isChanged = dest->getAbstractState().join(incoming);
	}
	return isChanged;
}
};

void analyze(Graph & g, Function * function) {
	WTOAnalysis().analyze(g, function);

}

template <typename Graph>
void printToDotBody(Graph & g) {
	for (Vertex vertex : BGLIterable(boost::vertices(g))) {
		if (g[vertex].basicBlock) {
			llvm::errs() << "n_" << vertex << " [label=\"" <<
					g[vertex].name << "\"];\n";
		} else {
			llvm::errs() << "subgraph cluster_n" << vertex << " {\n";
			Graph * pgraph = (Graph*)g[vertex].region;
			printToDotBody(*pgraph);
			llvm::errs() << "}\n";
		}
	}

	for (Edge edge : BGLIterable(boost::edges(g))) {
		Vertex source = boost::source(edge, g);
		if (g[source].basicBlock) {
			llvm::errs() << "n_" << source;
		} else {
			llvm::errs() << "cluster_n" << source;
		}
		llvm::errs() << " -> ";
		Vertex target = boost::target(edge, g);
		if (g[target].basicBlock) {
			llvm::errs() << "n_" << target;
		} else {
			llvm::errs() << "cluster_n" << target;
		}
		llvm::errs() << ";\n";
	}
}

template <typename Graph>
void printToDot(Graph & g) {
	llvm::errs() << "digraph G {\n";
	printToDotBody(g);
	llvm::errs() << "}\n";
}

Strategy * strategyFactory(CallGraph & callGraph, ChaoticExecution::StrategySelector a_strategy) {
	Strategy * strategy;
	switch (a_strategy) {
	case ChaoticExecution::ChaoticExecutionStrategy_BFS:
		strategy = new BFSStrategy();
		break;
	case ChaoticExecution::ChaoticExecutionStrategy_DFS:
		strategy = new DFSStrategy();
		break;
	case ChaoticExecution::ChaoticExecutionStrategy_WTO:
		strategy = new WTOStrategy();
		break;
	}
	strategy->m_callGraph = &callGraph;
	return strategy;
}

ChaoticExecution::ChaoticExecution(CallGraph & callGraph, ChaoticExecution::StrategySelector strategy) :
		callGraph(callGraph), m_strategy(strategyFactory(callGraph, strategy)) {
}

void ChaoticExecution::execute() {
	m_strategy->execute();
}


void ChaoticExecution::print() {
/*
	llvm::errs() << "Apron: Library " <<
			apron_manager->library <<
			", version " <<
			apron_manager->version << "\n";
	callGraph.printAsDot();
	std::set<BasicBlock *>::iterator it;
	for (it = seen.begin(); it != seen.end(); it++) {
		llvm::errs() << (*it)->toString() << "\n";
		ap_abstract1_t abst = ((*it)->getFunction()->trimAbstractValue((*it)->getAbstractState()));
		llvm::errs() << "Trimmed: " << (ap_abstract1_t*)&abst;
	}
	*/
}
