#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

struct Stack {
    int stack[100];
    int strcnt;
    std::map<int, std::string> stringtable;
    int sp;

    void push(int operand) {
        stack[sp++] = operand;
    }

    void pushs(const std::string &s) {
        int cnt = strcnt++;
        stringtable[cnt] = s;
        push(cnt);
    }

    int pop() {
        return stack[--sp];
    }

    std::string pops() {
        return stringtable[stack[--sp]];
    }
};

enum class OPType {
    NOP = 0,
    PUSHI,
    PUSHS,
    INTRINSIC
};

enum class IntrinsicType {
    EXIT = 0,
    PUTS
};

struct OP {
    OPType type;
    int operand;
    std::string soperand;
};

#define ENUMI(enum) static_cast<int>(enum)

#define OP_PLUS_I \
    OP { OPType::PLUSI, 0 }
#define OP_PUSH_I(op) \
    OP { OPType::PUSHI, op }
#define OP_PUSH_S(sop) \
    OP { OPType::PUSHS, 0, sop }
#define OP_INTRINSIC(intrinsic) \
    OP { OPType::INTRINSIC, ENUMI(intrinsic) }

#define INTRINSIC_EXIT OP_INTRINSIC(IntrinsicType::EXIT)
#define INTRINSIC_PUTS OP_INTRINSIC(IntrinsicType::PUTS)

std::string intrinsicToName(const IntrinsicType &type) {
    switch (type) {
    case IntrinsicType::EXIT:
        return "IntrinsicType::EXIT";
    case IntrinsicType::PUTS:
        return "IntrinsicType::PUTS";
    default:
        return "Unknown intrinsic.";
    }
}

std::string opTypeToName(const OP &op) {
    switch (op.type) {
    case OPType::PUSHI:
        return "OPType::PUSHI";
    case OPType::PUSHS:
        return "OPType::PUSHS";
    case OPType::INTRINSIC:
        return intrinsicToName(static_cast<IntrinsicType>(op.operand));
    default:
        return "";
    }
}

std::vector<OP> program = {};

void parseProgram(const std::string &source) {
    OP current = {};
    int cursor = 0;

    auto parseStr = [&]() {
        int start = ++cursor;
        int end = start;
        std::string str = {};
        while (source[end] != '"') {
            if (source[end] != '\\') {
                str += source[end];
                end++;
            } else {
                switch (source[end + 1]) {
                case 'n':
                    str += 0xA;
                    break;
                case 't':
                    str += 0x09;
                    break;
                default:
                    break;
                }
                end += 2;
            }
        }
        return make_pair(str, end + 1);
    };

    auto tryParseIdent = [&]() {
        int start = cursor;
        int end = start;
        while (std::isalpha(source[end]))
            end++;
        auto str = source.substr(start, end - start);
        return std::make_pair(str, end);
    };

    auto tryParseNumber = [&]() {
        int start = cursor;
        int end = start;
        while (std::isalnum(source[end]))
            end++;
        auto str = source.substr(start, end - start);
        return std::make_pair(str, end);
    };

    auto push_op = [&]() {
        program.emplace_back(current);
        printf("> Pushing op:\n");
        printf("    - Type: %s\n", opTypeToName(current).c_str());
        printf("    - Operand: %d\n", current.operand);
        printf("    - Str Operand: %s\n", current.soperand.c_str());
        current = {};
        cursor++;
    };

    while (cursor < source.length()) {
        switch (source[cursor]) {
        case ';':
            push_op();
            break;
        case '\n':
        case ' ':
            cursor++;
            break;
        case '"': {
            auto a = parseStr();
            current.soperand = a.first;
            cursor = a.second;
        } break;
        default:
            if (isalpha(source[cursor])) {
                auto res = tryParseIdent();

                if (res.first == "pushs") {
                    current.type = OPType::PUSHS;
                } else if (res.first == "puts") {
                    current = INTRINSIC_PUTS;
                } else if (res.first == "pushi") {
                    current.type = OPType::PUSHI;
                } else if (res.first == "exit") {
                    current = INTRINSIC_EXIT;
                }

                cursor = res.second;
            } else if (isalnum(source[cursor])) {
                auto res = tryParseNumber();
                current.operand = std::stoi(res.first);
                cursor = res.second;
            }
            break;
        }
    }
}

std::unique_ptr<llvm::LLVMContext> llvmContext;
std::unique_ptr<llvm::IRBuilder<>> irBuilder;
std::unique_ptr<llvm::Module> llvmModule;

void compileToLLVMIR() {

    llvmModule->getOrInsertFunction(
        "exit",
        llvm::FunctionType::get(irBuilder->getVoidTy(), {irBuilder->getInt32Ty()}, false));

    llvmModule->getOrInsertFunction(
        "printf",
        llvm::FunctionType::get(irBuilder->getVoidTy(), {irBuilder->getInt8PtrTy()}, false));

    auto main = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getInt32Ty(*llvmContext), {}, false),
        llvm::Function::ExternalLinkage, "main", llvmModule.get());

    auto bb = llvm::BasicBlock::Create(*llvmContext, "entry", main);
    irBuilder->SetInsertPoint(bb);

    auto stack = Stack{};
    for (size_t i = 0; i < program.size(); i++) {
        switch (program[i].type) {
        case OPType::PUSHI:
            stack.push(program[i].operand);
            break;
        case OPType::PUSHS:
            stack.pushs(program[i].soperand);
            break;
        case OPType::INTRINSIC: {
            switch (program[i].operand) {
            case ENUMI(IntrinsicType::EXIT): {
                int pop = stack.pop();
                irBuilder->CreateCall(
                    llvmModule->getFunction("exit"),
                    {llvm::ConstantInt::get(*llvmContext, llvm::APInt(32, pop, false))});
            } break;
            case ENUMI(IntrinsicType::PUTS): {
                std::string pop = stack.pops();
                auto str = irBuilder->CreateGlobalString(pop.c_str(), "");
                irBuilder->CreateCall(
                    llvmModule->getFunction("printf"),
                    {str});
            } break;
            }
        }
        default:
            break;
        }
    }

    irBuilder->CreateRet(llvm::ConstantInt::get(*llvmContext, llvm::APInt(32, 0, false)));
    verifyFunction(*main);
}

int main(int argc, char **argv) {

    (void)*argv++;
    if (argc > 1) {
        auto path = *argv++;

        std::ifstream source;
        source.open(path);
        std::stringstream sourcestream;
        sourcestream << source.rdbuf();
        source.close();
        parseProgram(sourcestream.str());
    }

    llvmContext = std::make_unique<llvm::LLVMContext>();
    llvmModule = std::make_unique<llvm::Module>("stacked", *llvmContext);
    irBuilder = std::make_unique<llvm::IRBuilder<>>(*llvmContext);

    llvmModule->print(llvm::errs(), nullptr);

    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    auto triple = llvm::sys::getDefaultTargetTriple();
    llvmModule->setTargetTriple(triple);

    std::string err;
    auto target = llvm::TargetRegistry::lookupTarget(triple, err);

    if (!target) {
        llvm::errs() << err;
        return 1;
    }

    auto CPU = "generic";

    llvm::TargetOptions opt;
    auto targetMachine = target->createTargetMachine(triple, CPU, "", opt, llvm::Reloc::PIC_);

    llvmModule->setDataLayout(targetMachine->createDataLayout());

    std::error_code errorcode;
    llvm::raw_fd_ostream dest("output.o", errorcode, llvm::sys::fs::OF_None);

    if (errorcode) {
        llvm::errs() << "Could not open file: " << errorcode.message();
        return 1;
    }

    llvm::legacy::PassManager pass;
    auto fileType = llvm::CodeGenFileType::CGFT_ObjectFile;

    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
        llvm::errs() << "Target machine can't emit a file of this type";
        return 1;
    }

    pass.run(*llvmModule);
    dest.flush();

    return 0;
}