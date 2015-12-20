std::string GenerateUniqueName(const char *root) {
    static int i = 0;
    char s[16];
    sprintf(s, "%s%d", root, i++);
    std::string S = s;
    return S;
}

std::string MakeLegalFunctionName(std::string Name) {
    std::string NewName;
    if (!Name.length())
        return GenerateUniqueName("anon_func_");

    // Start with what we have
    NewName = Name;

    // Look for a numberic first character
    if (NewName.find_first_of("0123456789") == 0) {
        NewName.insert(0, 1, 'n');
    }

    // Replace illegal characters with their ASCII equivalent
    std::string legal_elements =
        "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t pos;
    while ((pos = NewName.find_first_not_of(legal_elements)) !=
           std::string::npos) {
        char old_c = NewName.at(pos);
        char new_str[16];
        sprintf(new_str, "%d", (int)old_c);
        NewName = NewName.replace(pos, 1, new_str);
    }

    return NewName;
}

class MCJITHelper {
public:
    MCJITHelper(llvm::LLVMContext &C) : Context(C), OpenModule(NULL) {}
    ~MCJITHelper();

    llvm::Function *getFunction(const std::string FnName);
    llvm::Module *getModuleForNewFunction();
    void *getPointerToFunction(llvm::Function *F);
    void *getSymbolAddress(const std::string &Name);
    void dump();

private:
    typedef std::vector<llvm::Module *> ModuleVector;
    typedef std::vector<llvm::ExecutionEngine *> EngineVector;

    llvm::LLVMContext &Context;
    llvm::Module *OpenModule;
    ModuleVector Modules;
    EngineVector Engines;
};

class HelpingMemoryManager : public llvm::SectionMemoryManager {
    HelpingMemoryManager(const HelpingMemoryManager &) = delete;
    void operator=(const HelpingMemoryManager &) = delete;

public:
    HelpingMemoryManager(MCJITHelper *Helper) : MasterHelper(Helper) {}
    ~HelpingMemoryManager() override {}

    /// This method returns the address of the specified symbol.
    /// Our implementation will attempt to find symbols in other
    /// modules associated with the MCJITHelper to cross link symbols
    /// from one generated module to another.
    uint64_t getSymbolAddress(const std::string &Name) override;

private:
    MCJITHelper *MasterHelper;
};

uint64_t HelpingMemoryManager::getSymbolAddress(const std::string &Name) {
    uint64_t FnAddr = SectionMemoryManager::getSymbolAddress(Name);
    if (FnAddr)
        return FnAddr;

    uint64_t HelperFun = (uint64_t)MasterHelper->getSymbolAddress(Name);

    if (!HelperFun && Name[0] == '_') {
        FnAddr = SectionMemoryManager::getSymbolAddress(Name.substr(1));
        if (FnAddr)
            return FnAddr;

        HelperFun = (uint64_t)MasterHelper->getSymbolAddress(Name.substr(1));


        if (!HelperFun)
            llvm::report_fatal_error("Program used extern function '" + Name +
                                     "' which could not be resolved!");
    }

    return HelperFun;
}

MCJITHelper::~MCJITHelper() {
    if (OpenModule)
        delete OpenModule;
    EngineVector::iterator begin = Engines.begin();
    EngineVector::iterator end = Engines.end();
    EngineVector::iterator it;
    for (it = begin; it != end; ++it)
        delete *it;
}

llvm::Function *MCJITHelper::getFunction(const std::string FnName) {
    ModuleVector::iterator begin = Modules.begin();
    ModuleVector::iterator end = Modules.end();
    ModuleVector::iterator it;
    for (it = begin; it != end; ++it) {
        llvm::Function *F = (*it)->getFunction(FnName);
        if (F) {
            if (*it == OpenModule)
                return F;

            assert(OpenModule != NULL);

            // This function is in a module that has already been JITed.
            // We need to generate a new prototype for external linkage.
            llvm::Function *PF = OpenModule->getFunction(FnName);
            if (PF && !PF->empty()) {
                ErrorF("redefinition of function across modules");
                return 0;
            }

            // If we don't have a prototype yet, create one.
            if (!PF)
                PF = llvm::Function::Create(F->getFunctionType(), llvm::Function::ExternalLinkage,
                                            FnName, OpenModule);
            return PF;
        }
    }
    return NULL;
}

llvm::Module *MCJITHelper::getModuleForNewFunction() {
    // If we have a Module that hasn't been JITed, use that.
    if (OpenModule)
        return OpenModule;

    // Otherwise create a new Module.
    std::string ModName = GenerateUniqueName("mcjit_module_");
    llvm::Module *M = new llvm::Module(ModName, Context);
    Modules.push_back(M);
    OpenModule = M;
    return M;
}

void *MCJITHelper::getPointerToFunction(llvm::Function *F) {
    // See if an existing instance of MCJIT has this function.
    EngineVector::iterator begin = Engines.begin();
    EngineVector::iterator end = Engines.end();
    EngineVector::iterator it;
    for (it = begin; it != end; ++it) {
        void *P = (*it)->getPointerToFunction(F);
        if (P)
            return P;
    }

    // If we didn't find the function, see if we can generate it.
    if (OpenModule) {
        std::string ErrStr;
        llvm::ExecutionEngine *NewEngine =
            llvm::EngineBuilder(std::unique_ptr<llvm::Module>(OpenModule))
            .setErrorStr(&ErrStr)
            .setMCJITMemoryManager(std::unique_ptr<HelpingMemoryManager>(
                                       new HelpingMemoryManager(this)))
            .create();
        if (!NewEngine) {
            fprintf(stderr, "Could not create ExecutionEngine: %s\n", ErrStr.c_str());
            exit(1);
        }

        // Create a function pass manager for this engine
        auto *FPM = new llvm::legacy::FunctionPassManager(OpenModule);

        // Set up the optimizer pipeline.  Start with registering info about how the
        // target lays out data structures.
        OpenModule->setDataLayout(NewEngine->getDataLayout());
        // Provide basic AliasAnalysis support for GVN.
        FPM->add(llvm::createBasicAliasAnalysisPass());
        // Promote allocas to registers.
        FPM->add(llvm::createPromoteMemoryToRegisterPass());
        // Do simple "peephole" optimizations and bit-twiddling optzns.
        FPM->add(llvm::createInstructionCombiningPass());
        // Reassociate expressions.
        FPM->add(llvm::createReassociatePass());
        // Eliminate Common SubExpressions.
        FPM->add(llvm::createGVNPass());
        // Simplify the control flow graph (deleting unreachable blocks, etc).
        FPM->add(llvm::createCFGSimplificationPass());
        FPM->doInitialization();

        // For each function in the module
        llvm::Module::iterator it;
        llvm::Module::iterator end = OpenModule->end();
        for (it = OpenModule->begin(); it != end; ++it) {
            // Run the FPM on this function
            FPM->run(*it);
        }

        // We don't need this anymore
        delete FPM;

        OpenModule = NULL;
        Engines.push_back(NewEngine);
        NewEngine->finalizeObject();
        return NewEngine->getPointerToFunction(F);
    }
    return NULL;
}

void *MCJITHelper::getSymbolAddress(const std::string &Name) {
    // Look for the symbol in each of our execution engines.
    EngineVector::iterator begin = Engines.begin();
    EngineVector::iterator end = Engines.end();
    EngineVector::iterator it;

    for (it = begin; it != end; ++it) {
        uint64_t FAddr = (*it)->getFunctionAddress(Name);
        if (FAddr) {
            return (void *)FAddr;
        }
    }
    
    return NULL;
}

void MCJITHelper::dump() {
    ModuleVector::iterator begin = Modules.begin();
    ModuleVector::iterator end = Modules.end();
    ModuleVector::iterator it;
    for (it = begin; it != end; ++it)
        (*it)->dump();
}
