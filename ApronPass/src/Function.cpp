#include <AbstractState.h>
#include <APStream.h>
#include <Function.h>
#include <BasicBlock.h>

#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalAlias.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>

FunctionManager FunctionManager::instance;
FunctionManager & FunctionManager::getInstance() {
	return instance;
}

Function * FunctionManager::getFunction(llvm::Function * function) {
	std::map<llvm::Function *, Function *>::iterator it =
			instances.find(function);
	Function * result;
	if (it == instances.end()) {
		result = new Function(function);
		instances.insert(std::make_pair(function, result));
	} else {
		result = it->second;
	}
	return result;
}

Function * FunctionManager::getFunction(llvm::GlobalAlias * alias) {
	llvm::GlobalValue * aliasee = alias->getAliasedGlobal();
	llvm::Function * function = llvm::dyn_cast<llvm::Function>(aliasee);
	if (!function) {
		return 0;
	}
	std::map<llvm::Function *, Function *>::iterator it =
			instances.find(function);
	Function * result;
	if (it == instances.end()) {
		result = new Alias(alias, function);
		instances.insert(std::make_pair(function, result));
	} else {
		result = it->second;
	}
	return result;
}

Function::Function(llvm::Function * function) : m_function(function), m_name(function->getName()),
		m_returnValueState(ApronAbstractState::bottom()) {
	populateUserPointers();
}

void Function::populateUserPointers() {
	const llvm::Function::ArgumentListType & arguments = m_function->getArgumentList();
	for (const llvm::Argument & argument : arguments) {
		if (argument.getType()->isPointerTy()) {
			std::string name = argument.getName().str();
			m_userPointers.insert(name);
		}
	}
	llvm::errs() << "User pointers (function):\n";
	for (const std::string & ptr : m_userPointers) {
		llvm::errs() << "\t" << ptr << "\n";
	}
}

bool Function::isUserPointer(std::string & ptrname) {
	//return ptrname.find("buf") == 0;
	return (m_userPointers.count(ptrname) == 1);
}

llvm::ReturnInst * Function::getReturnInstruction() {
	llvm::Function &F = *m_function;
	for (auto bbit = F.begin(), bbie = F.end(); bbit != bbie; bbit++) {
		llvm::BasicBlock & bb = *bbit;
		for (auto iit = bb.begin(), iie = bb.end(); iit != iie; iit++) {
			llvm::Instruction & inst = *iit;
			if (llvm::isa<llvm::ReturnInst>(inst)) {
				llvm::ReturnInst & result = llvm::cast<llvm::ReturnInst>(inst);
				return &result;
			}
		}
	}
	return NULL;
}

BasicBlock * Function::getReturnBasicBlock() {
	llvm::ReturnInst * returnInst = getReturnInstruction();
	llvm::BasicBlock * basicBlock = returnInst->getParent();
	BasicBlockManager & basicBlockManager = BasicBlockManager::getInstance();
	return basicBlockManager.getBasicBlock(basicBlock);
}

const std::string & Function::getReturnValueName() {
	llvm::ReturnInst * returnInst = getReturnInstruction();
	llvm::Value * returnValue = returnInst->getReturnValue();
	ValueFactory * factory = ValueFactory::getInstance();
	Value * returnValueValue = factory->getValue(returnValue);
	return returnValueValue->getName();
}

ApronAbstractState & Function::getReturnValue() {
	return m_returnValueState;
}

bool Function::isSizeVariable(const char * varname) {
	if ((strncmp(varname, "size(", sizeof("size(")-1) == 0) &&
			varname[strlen(varname)-1] == ')') {
		return true;
	}
	return false;
}

bool Function::isOffsetVariable(const char * varname) {
	if ((strncmp(varname, "offset(", sizeof("offset(")-1) == 0) &&
			varname[strlen(varname)-1] == ')') {
		return true;
	}
	return false;
}

bool Function::isLastVariable(const char * varname) {
	if ((strncmp(varname, "last(", sizeof("last(")-1) == 0) &&
			varname[strlen(varname)-1] == ')') {
		return true;
	}
	return false;
}

bool Function::isFunctionParameter(const char * varname) {
	const llvm::Function::ArgumentListType & arguments = m_function->getArgumentList();
	for (const llvm::Argument & argument : arguments) {
		if (argument.getName() == varname) {
			return true;
		}
	}
	return false;
}

bool Function::isFunctionParameterDereference(const char * varname) {
	const llvm::Function::ArgumentListType & arguments = m_function->getArgumentList();
	for (const llvm::Argument & argument : arguments) {
		if (m_userPointers.count(argument.getName()) <= 0) {
			continue;
		}
		const std::string & argumentDeref =
				AbstractState::generateBufferDereferenceName(
						argument.getName());
		if (argumentDeref == varname) {
			return true;
		}
	}
	return false;
}

bool Function::isReturnValue(const char * varname) {
	llvm::ReturnInst * returnInst = getReturnInstruction();
	llvm::Value * returnValue = returnInst->getReturnValue();
	return (returnValue && (returnValue->getName() == varname));
}

bool Function::isStructBuffer(const char * varname, const std::string & structname) {
	const llvm::Function::ArgumentListType & arguments = m_function->getArgumentList();
	for (const llvm::Argument & argument : arguments) {
		if (argument.getName()  != varname) {
			continue;
		}
		llvm::Type * type = argument.getType();
		if (!type->isPointerTy()) {
			llvm::errs() << "Function::isStructBuffer 1: " << varname << " of type " << *type << " is not a " << structname << "\n";
			break;
		}
		llvm::Type * pointedType = type->getPointerElementType();
		if (!pointedType->isStructTy()) {
			llvm::errs() << "Function::isStructBuffer 2: " << varname << " of type " << *type << " is not a " << structname << "\n";
			break;
		}
		if (pointedType->getStructName() != structname) {
			llvm::errs() << "Function::isStructBuffer 3: " << varname << " of type " << *type << " is not a " << structname << "\n";
			break;
		}
		return true;
	}
	return false;
}

bool Function::isIovecBuffer(const char * varname) {
	return isStructBuffer(varname, "struct.iovec");
}

bool Function::isMsghdrBuffer(const char * varname) {
	return isStructBuffer(varname, "struct.msghdr");
}

bool Function::isVarInOut(const char * varname) {
	// Return true iff:
	// 	varname is argument
	// 	varname is last(*)
	// 	varname is size(*)
	// 	varname is the return value
	// XXX Memoize this value?
	return isSizeVariable(varname) ||
			// isOffsetVariable(varname) ||
			isLastVariable(varname) ||
			isReturnValue(varname) ||
			isFunctionParameter(varname) ||
			isFunctionParameterDereference(varname);
}

ap_abstract1_t Function::trimAbstractValue(AbstractState & state) {
	ApronAbstractState apronAbstractState = state.m_apronAbstractState;
	ap_abstract1_t & asAbstract1 = apronAbstractState.m_abstract1;
	ap_manager_t * manager = apron_manager;
	ap_environment_t * environment = ap_abstract1_environment(manager, &asAbstract1);

	// Forget all variables that are not arguments, 'last(*,*)', or the return value
	std::vector<ap_var_t> forgetVars;
	int env_size = environment->intdim;
	for (int cnt = 0; cnt < env_size; cnt++) {
		ap_var_t var = ap_environment_var_of_dim(environment, cnt);
		const char * varName = (const char*)var;
		if (!isVarInOut(varName)) {
			forgetVars.push_back(var);
		}
	}
	ap_abstract1_t result = ap_abstract1_forget_array(manager, false, &asAbstract1,
			forgetVars.data(), forgetVars.size(), false);
	result = ap_abstract1_minimize_environment(manager, true, &result);
	return result;
}

AbstractState & Function::getReturnAbstractState() {
	BasicBlock * returnBasicBlock = getReturnBasicBlock();
	AbstractState & as = returnBasicBlock->getAbstractState();
	return as;
}

std::vector<std::string> Function::getUserPointers() {
	std::vector<std::string> result;
	for (const std::string & buffer : m_userPointers) {
		result.push_back(buffer);
	}
	return result;
}

std::vector<std::string> Function::getConstrainedUserPointers(AbstractState & state) {
	std::vector<std::string> userBuffers = getUserPointers();
	std::vector<std::string> result;
	for (const std::string & userBuffer : userBuffers) {
		const std::string & lastName = state.generateLastName(userBuffer, user_pointer_operation_write);
		if (state.m_apronAbstractState.isConstrained(lastName)) {
			result.push_back(userBuffer);
		}
	}
	return result;
}

void Function::pushBackIfConstrainsUserPointers(
		std::map<std::string, ApronAbstractState> & result,
		AbstractState & state,
		std::vector<std::string> & userBuffers) {
	std::vector<std::string> constrainedBuffers = getConstrainedUserPointers(state);
	for (const std::string & userBuffer : constrainedBuffers) {
		result.insert(std::make_pair(userBuffer, state.m_apronAbstractState));
	}
}

void Function::insertErrorState(std::multimap<std::string, ApronAbstractState> & states,
		const ApronAbstractState & baseState,
		const std::string & userBuffer, user_pointer_operation_e op,
		bool defendNullWrite) {
	const std::string & lastName = AbstractState::generateLastName(userBuffer, op);
	if (!baseState.isKnown(lastName)) {
		return;
	}
	auto copypair = states.insert(std::make_pair(
			userBuffer, baseState));
	ApronAbstractState & copy = copypair->second;
	// Add constraint: last() > size
	const std::string & sizeName = AbstractState::generateSizeName(userBuffer);
	copy.extend(lastName);
	copy.extend(sizeName);
	ap_texpr1_t * lastExpr = copy.asTexpr(lastName);
	ap_texpr1_t * sizeExpr = copy.asTexpr(sizeName);
	ap_texpr1_t * diff = ap_texpr1_binop(
			AP_TEXPR_SUB, lastExpr, sizeExpr,
			AP_RTYPE_INT, AP_RDIR_ZERO);
	ap_tcons1_t cons = ap_tcons1_make(AP_CONS_SUP, diff, copy.zero());
	copy.meet(cons);
	if (!defendNullWrite) {
		return;
	}
	sizeExpr = copy.asTexpr(sizeName);
	cons = ap_tcons1_make(AP_CONS_SUP, sizeExpr, copy.zero());
	copy.meet(cons);
}

std::multimap<std::string, ApronAbstractState> Function::getErrorStates() {
	std::multimap<std::string, ApronAbstractState> result;
	std::vector<std::string> userBuffers = getUserPointers();
	for (auto & memOpsState : m_successMemOpsAbstractStates) {
		for (std::string & userBuffer : userBuffers) {
			ApronAbstractState & baseState = memOpsState.second.m_apronAbstractState;
			bool defendNullWrite = !memOpsState.second.isPossiblyAccessToNull(userBuffer);
			insertErrorState(result, baseState, userBuffer, user_pointer_operation_write, defendNullWrite);
			insertErrorState(result, baseState, userBuffer, user_pointer_operation_read, defendNullWrite);
		}
	}
	return result;
}

std::map<std::string, AbstractState> Function::getSuccessStates() {
	std::map<std::string, AbstractState> result;
	for (auto & memOpsState : m_successMemOpsAbstractStates) {
		AbstractState & successState = memOpsState.second;
		for (std::string & userBuffer : getUserPointers()) {
			AbstractState copy = successState;
			if (!copy.isPossiblyAccessToNull(userBuffer)) {
				const std::string & sizeName = AbstractState::generateSizeName(userBuffer);
				ap_texpr1_t * sizeExpr = copy.m_apronAbstractState.asTexpr(sizeName);
				ap_tcons1_t cons = ap_tcons1_make(AP_CONS_SUP,
						sizeExpr, copy.m_apronAbstractState.zero());
				copy.m_apronAbstractState.meet(cons);
			}
			result[userBuffer].join(copy);
		}
	}
	return result;
}

ApronAbstractState Function::minimize(const ApronAbstractState & state) {
	// Forget all variables that are not arguments, 'last(*,*)', size(*),
	// or the return value
	std::vector<ap_var_t> forgetVars;
	ap_environment_t * environment = state.getEnvironment();
	int env_size = environment->intdim;
	for (int cnt = 0; cnt < env_size; cnt++) {
		ap_var_t var = ap_environment_var_of_dim(environment, cnt);
		const char * varName = (const char*)var;
		if (!isVarInOut(varName)) {
			forgetVars.push_back(var);
		}
	}
	if (forgetVars.empty()) {
		return state;
	}
	ap_abstract1_t abstract1 = state.m_abstract1;
	ap_abstract1_t result = ap_abstract1_forget_array(apron_manager, false, &abstract1,
			forgetVars.data(), forgetVars.size(), false);
	result = ap_abstract1_minimize_environment(apron_manager, false, &result);
	ap_abstract1_minimize(apron_manager, &result);
	return result;
}

ApronAbstractState Function::minimizeFurther(const ApronAbstractState & state) {
	// Forget all variables that are not arguments, 'last(*,*)', size(*),
	// or the return value
	std::vector<ap_var_t> forgetVars;
	ap_environment_t * environment = state.getEnvironment();
	int env_size = environment->intdim;
	for (int cnt = 0; cnt < env_size; cnt++) {
		ap_var_t var = ap_environment_var_of_dim(environment, cnt);
		const char * varName = (const char*)var;
		if (!isVarInOut(varName)) {
			forgetVars.push_back(var);
		} else if (isLastVariable(varName)) {
			forgetVars.push_back(var);
		}
	}
	if (forgetVars.empty()) {
		return state;
	}
	ap_abstract1_t abstract1 = state.m_abstract1;
	ap_abstract1_t result = ap_abstract1_forget_array(apron_manager, false, &abstract1,
			forgetVars.data(), forgetVars.size(), false);
	result = ap_abstract1_minimize_environment(apron_manager, false, &result);
	return result;
}

const std::string & Function::getName() const {
	return m_name;
}

std::string Function::getTypeString(llvm::Type * type) {
		std::string result;
		llvm::raw_string_ostream rso(result);
		if (!type->isPointerTy()) {
			rso << *type;
			return rso.str();
		}
		llvm::Type * pointedType = type->getPointerElementType();
		if (pointedType->isStructTy()) {
			if (pointedType->getStructName() == "struct.iovec") {
				return "struct iovec *";
			}
			if (pointedType->getStructName() == "struct.user_msghdr") {
				return "struct msghdr *";
			}
			rso << "/*" << pointedType->getStructName() << "*/ char *";
			return rso.str();
		}
		rso << getTypeString(pointedType) << " *";
		return rso.str();
}

std::string Function::getReturnTypeString() {
	return getTypeString(m_function->getReturnType());
}

std::vector<std::pair<std::string, std::string> > Function::getArgumentStrings() {
	std::vector<std::pair<std::string, std::string> > result;
	const llvm::Function::ArgumentListType & arguments = m_function->getArgumentList();
	for (const llvm::Argument & argument : arguments) {
		result.push_back(std::make_pair(getTypeString(argument.getType()), argument.getName()));
	}
	return result;
}

std::string Function::getSignature() {
	std::string result;
	llvm::raw_string_ostream rso(result);
	rso << getTypeString(m_function->getReturnType()) << " " << getName() << "(";
	auto arguments = getArgumentStrings();
	bool first = true;
	for (auto & argumentPair : arguments) {
		if (!first) {
			rso << ", ";
		}
		first = false;
		rso << argumentPair.first << " " << argumentPair.second;
	}
	rso << ")";
	return rso.str();
}

BasicBlock * Function::getRoot() const {
	BasicBlockManager & factory = BasicBlockManager::getInstance();
	llvm::BasicBlock & llvmEntry = m_function->getEntryBlock();
	BasicBlock * root = factory.getBasicBlock(&llvmEntry);
	return root;
}

llvm::Function * Function::getLLVMFunction() {
	return m_function;
}

Alias::Alias(llvm::GlobalAlias * alias, llvm::Function * function) :
		m_alias(alias), Function(function) {
	m_name = alias->getName();
	m_userPointers.clear();
	populateUserPointers();
}

void Alias::populateUserPointers() {
	const llvm::Function::ArgumentListType & arguments = m_function->getArgumentList();
	llvm::FunctionType * ftype = getFunctionType();
	int idx = 0;
	for (const llvm::Argument & argument : arguments) {
		if (ftype->getParamType(idx)->isPointerTy()) {
			std::string name = argument.getName().str();
			m_userPointers.insert(name);
		}
		idx++;
	}
	llvm::errs() << "User pointers (alias):\n";
	for (const std::string & ptr : m_userPointers) {
		llvm::errs() << "\t" << ptr << "\n";
	}
}

llvm::FunctionType * Alias::getFunctionType() {
	llvm::Value * aliasee = m_alias->getAliasee();
	llvm::ConstantExpr * asConstExpr = llvm::dyn_cast<llvm::ConstantExpr>(aliasee);
	if (asConstExpr) {
		llvm::Instruction * bitCastInst = asConstExpr->getAsInstruction();
		llvm::Type * type = bitCastInst->getType();
		llvm::FunctionType * ftype = llvm::dyn_cast<llvm::FunctionType>(type->getPointerElementType());
		delete bitCastInst;
		return ftype;
	}

	llvm::GlobalValue * asGlobVal = m_alias->getAliasedGlobal();
	if (asGlobVal) {
		llvm::Function * function = llvm::dyn_cast<llvm::Function>(asGlobVal);
		if (function) {
			return function->getFunctionType();
		}
		abort();
	}
	abort();
}

std::vector<std::pair<std::string, std::string> > Alias::getArgumentStrings() {
	std::vector<std::pair<std::string, std::string> > result;
	const llvm::Function::ArgumentListType & arguments = m_function->getArgumentList();
	llvm::FunctionType * ftype = getFunctionType();
	int idx = 0;
	for (const llvm::Argument & argument : arguments) {
		result.push_back(std::make_pair(getTypeString(ftype->getParamType(idx)), argument.getName()));
		idx++;
	}
	return result;
}

bool Alias::isStructBuffer(const char * varname, const std::string & structname) {
	const llvm::Function::ArgumentListType & arguments = m_function->getArgumentList();
	llvm::FunctionType * ftype = getFunctionType();
	int idx = 0;
	for (const llvm::Argument & argument : arguments) {
		llvm::Type * type = ftype->getParamType(idx);
		idx++;
		if (argument.getName()  != varname) {
			continue;
		}
		if (!type->isPointerTy()) {
			llvm::errs() << "Alias::isStructBuffer 1: " << varname << " of type " << *type << " is not a " << structname << "\n";
			break;
		}
		llvm::Type * pointedType = type->getPointerElementType();
		if (!pointedType->isStructTy()) {
			llvm::errs() << "Alias::isStructBuffer 2: " << varname << " of type " << *type << " is not a " << structname << "\n";
			break;
		}
		if (pointedType->getStructName() != structname) {
			llvm::errs() << "Alias::isStructBuffer 3: " << varname << " of type " << *type << " is not a " << structname << "\n";
			break;
		}
		return true;
	}
	return false;
}

