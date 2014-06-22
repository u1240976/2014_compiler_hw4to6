#include <stdio.h>
#include <assert.h>
#include "codeGen.h"
#include "header.h"
#include "symbolTable.h"
#include "semanticAnalysis.h"

#define GLOBAL 1
#define LOCAL 2
GlobalResource GR;

void codeGen(FILE* targetFile, AST_NODE* prog, STT* symbolTable){
    AST_NODE* child = prog->child;
    while(child){
        if(child->nodeType == VARIABLE_DECL_LIST_NODE)
            genVariableDeclList(targetFile, symbolTable, child);
        else if(child->nodeType == DECLARATION_NODE)
            genFuncDecl(targetFile, symbolTable, child);
        child = child->rightSibling;
    }
}

/*** variable declaration ***/
void genVariableDeclList(FILE* targetFile, STT* symbolTable, AST_NODE* variableDeclListNode){
    /* codegen Declaration List */
    AST_NODE* child = variableDeclListNode->child;

    int kind = LOCAL;
    if(symbolTable->currentLevel == 0)
        kind = GLOBAL;

    if(kind == GLOBAL){
        fprintf(targetFile, ".data\n");
    }

    while(child){
        genVariableDecl(targetFile, symbolTable, child, kind);
        child = child->rightSibling; 
    }

    if(kind == GLOBAL)
        return;
}

void genVariableDecl(FILE* targetFile, STT* symbolTable, AST_NODE* declarationNode, 
  int kind){
    /* kind: GLOBAL or LOCAL variable
     * Notice: it can more than one variable declaration, like int a, b, c;
     */
    /* no initial value now */
    DECL_KIND declKind = declarationNode->semantic_value.declSemanticValue.kind;
    if(declKind != VARIABLE_DECL)
        return;

    AST_NODE* variableNode = declarationNode->child->rightSibling;

    while(variableNode){
        char* varName = variableNode->semantic_value.identifierSemanticValue.identifierName;
        SymbolTableEntry* entry = lookupSymbol(symbolTable, varName);
        TypeDescriptor* type = entry->type;

        int varSize = 4; // int and float
        int i;
        for(i=0; i<type->dimension; i++){
            varSize *= type->sizeOfEachDimension[i]; 
        }

        if(kind == GLOBAL){
            if(type->dimension != 0)
                fprintf(targetFile, "%s: .space %d\n", entry->name, varSize);
            else if(type->primitiveType == INT_TYPE)
                fprintf(targetFile, "%s: .word 0\n", entry->name);
            else if(type->primitiveType == FLOAT_TYPE)
                fprintf(targetFile, "%s: .float 0.0\n", entry->name);
        }
        else if(kind == LOCAL){
            entry->stackOffset = GR.stackTop + 4;
            GR.stackTop += varSize;
        }

        variableNode = variableNode->rightSibling;
    }
}

/*** function implementation ***/
void genFuncDecl(FILE* targetFile, STT* symbolTable, AST_NODE* declarationNode){
    /* codegen for function definition */
    AST_NODE* returnTypeNode = declarationNode->child;
    AST_NODE* funcNameNode = returnTypeNode->rightSibling;
    AST_NODE* paraListNode = funcNameNode->rightSibling;
    AST_NODE* blockNode = paraListNode->rightSibling;

    char* funcName = funcNameNode->semantic_value.identifierSemanticValue.identifierName;
    genFuncHead(targetFile, funcName);

    /*
     * parameter list
     */

    /* into block: openScope & prologue
     *             processing Decl_list + Stmt_list
     *             epilogue & closeScope
     */

    openScope(symbolTable, USE);
    genPrologue(targetFile, funcName);

    AST_NODE* blockChild = blockNode->child;

    while(blockChild){
        if(blockChild->nodeType == VARIABLE_DECL_LIST_NODE)
            genVariableDeclList(targetFile, symbolTable, blockChild);
        if(blockChild->nodeType == STMT_LIST_NODE)
            genStmtList(targetFile, symbolTable, blockChild, funcName);
        blockChild = blockChild->rightSibling;
    }

    genEpilogue(targetFile, funcName, GR.stackTop);
    GR.stackTop = 36;
    closeScope(symbolTable);
}

void genFuncHead(FILE* targetFile, char* funcName){
    fprintf(targetFile, ".text\n"                     );
    fprintf(targetFile, "%s:\n"                       , funcName);
}

void genPrologue(FILE* targetFile, char* funcName){
    fprintf(targetFile, "    sw $ra, 0($sp)\n"        );
    fprintf(targetFile, "    sw $fp, -4($sp)\n"       );
    fprintf(targetFile, "    add $fp, $sp, -4\n"      );
    fprintf(targetFile, "    add $sp, $fp, -4\n"      );
    fprintf(targetFile, "    lw  $v0, _framesize_%s\n" , funcName);
    fprintf(targetFile, "    sub $sp, $sp, $v0\n"      );
    fprintf(targetFile, "    # Saved register\n"      );
    fprintf(targetFile, "    sw  $s0, -36($fp)\n"      );
    fprintf(targetFile, "    sw  $s1, -32($fp)\n"      );
    fprintf(targetFile, "    sw  $s2, -28($fp)\n"      );
    fprintf(targetFile, "    sw  $s3, -24($fp)\n"      );
    fprintf(targetFile, "    sw  $s4, -20($fp)\n"      );
    fprintf(targetFile, "    sw  $s5, -16($fp)\n"      );
    fprintf(targetFile, "    sw  $s6, -12($fp)\n"      );
    fprintf(targetFile, "    sw  $s7, -8($fp)\n"       );
    fprintf(targetFile, "    sw  $gp, -4($fp)\n"       ); 
    fprintf(targetFile, "_begin_%s:\n"                , funcName);
}                                               

void genEpilogue(FILE* targetFile, char* funcName, int frameSize){
    fprintf(targetFile, "# epilogue\n"               );
    fprintf(targetFile, "_end_%s:\n"                 , funcName);
    fprintf(targetFile, "    # Load Saved register\n");
    fprintf(targetFile, "    lw  $s0, -36($fp)\n"     );
    fprintf(targetFile, "    lw  $s1, -32($fp)\n"     );
    fprintf(targetFile, "    lw  $s2, -28($fp)\n"     );
    fprintf(targetFile, "    lw  $s3, -24($fp)\n"     );
    fprintf(targetFile, "    lw  $s4, -20($fp)\n"     );
    fprintf(targetFile, "    lw  $s5, -16($fp)\n"     );
    fprintf(targetFile, "    lw  $s6, -12($fp)\n"     );
    fprintf(targetFile, "    lw  $s7, -8($fp)\n"      );
    fprintf(targetFile, "    lw  $gp, -4($fp)\n"      );
    fprintf(targetFile, "\n"                         );
    fprintf(targetFile, "    lw  $ra, 4($fp)\n"      );
    fprintf(targetFile, "    add $sp, $fp, 4\n"      );
    fprintf(targetFile, "    lw  $fp, 0($fp)\n"      );
    fprintf(targetFile, "    jr  $ra\n"              );
    fprintf(targetFile, ".data\n"                    );
    fprintf(targetFile, "    _framesize_%s: .word %d\n", funcName, frameSize);
}

/*** statement generation ***/
void genStmtList(FILE* targetFile, STT* symbolTable, AST_NODE* stmtListNode, char* funcName){
    
    AST_NODE* child = stmtListNode->child;

    while( child ){
        genStmt(targetFile, symbolTable, child, funcName);
        child = child -> rightSibling;
    }
}

void genStmt(FILE* targetFile, STT* symbolTable, AST_NODE* stmtNode, char* funcName){
    
    if( stmtNode->nodeType == BLOCK_NODE )
        genBlock(targetFile, symbolTable, stmtNode, funcName);
    else if( stmtNode->nodeType == STMT_NODE ){
        
        STMT_KIND stmtKind = stmtNode->semantic_value.stmtSemanticValue.kind;
        switch( stmtKind ){
            case WHILE_STMT: genWhileStmt(targetFile, symbolTable, stmtNode, funcName); break;
            case FOR_STMT: assert(0); break;
            case IF_STMT: genIfStmt(targetFile, symbolTable, stmtNode, funcName); break;
            case ASSIGN_STMT: genAssignmentStmt(targetFile, symbolTable, stmtNode); break;
            case FUNCTION_CALL_STMT: genFuncCallStmt(targetFile, symbolTable, stmtNode, funcName); break;
            case RETURN_STMT: genReturnStmt(targetFile, symbolTable, stmtNode, funcName); break;
        }
    }
}

void genBlock(FILE* targetFile, STT* symbolTable, AST_NODE* blockNode, char* funcName){
    openScope(symbolTable, USE);

    AST_NODE* blockChild = blockNode->child;
    while(blockChild){
        if(blockChild->nodeType == VARIABLE_DECL_LIST_NODE)
            genVariableDeclList(targetFile, symbolTable, blockChild);
        if(blockChild->nodeType == STMT_LIST_NODE)
            genStmtList(targetFile, symbolTable, blockChild, funcName);
        blockChild = blockChild->rightSibling;
    }

    closeScope(symbolTable);
}

void genIfStmt(FILE* targetFile, STT* symbolTable, AST_NODE* ifStmtNode, char* funcName){
    
    // condition
    genAssignExpr(targetFile, symbolTable, ifStmtNode->child);
    
    // jump to else if condition not match
    int elseLabel = GR.labelCounter++;
    int exitLabel = GR.labelCounter++;
    fprintf(targetFile, "beqz $%d L%d\n", ifStmtNode->child->valPlace.place.regNum, elseLabel);
    
    // then block
    genStmt(targetFile, symbolTable, ifStmtNode->child->rightSibling, funcName);
    
    // jump over else
    fprintf(targetFile, "j L%d\n", exitLabel);
    fprintf(targetFile, "L%d:\n", elseLabel);

    // else block
    if( ifStmtNode->child->rightSibling->rightSibling->nodeType != NUL_NODE )
        genStmt(targetFile, symbolTable, ifStmtNode->child->rightSibling->rightSibling, funcName);

    // exit
    fprintf(targetFile, "L%d:\n", exitLabel);
}

void genWhileStmt(FILE* targetFile, STT* symbolTable, AST_NODE* whileStmtNode, char* funcName){
    
    // Test Label
    int testLabel = GR.labelCounter++;
    int exitLabel = GR.labelCounter++;
    fprintf(targetFile, "L%d:\n", testLabel);
    
    // condition
    genAssignExpr(targetFile, symbolTable, whileStmtNode->child);
    
    // check condition
    fprintf(targetFile, "beqz $%d L%d\n", whileStmtNode->child->valPlace.place.regNum, exitLabel);
    
    // Stmt
    genStmt(targetFile, symbolTable, whileStmtNode->child->rightSibling, funcName);

    // loop back
    fprintf(targetFile, "j L%d\n", testLabel);

    // exit
    fprintf(targetFile, "L%d:\n", exitLabel);
}

void genFuncCallStmt(FILE* targetFile, STT* symbolTable, AST_NODE* exprNode, char* funcName){
    char* callingFuncName = exprNode->child->semantic_value.identifierSemanticValue.identifierName;
    if(strncmp(callingFuncName, "read", 4) == 0)
        genRead(targetFile);
    else if(strncmp(callingFuncName, "fread", 5) == 0)
        genFRead(targetFile);
    else if(strncmp(callingFuncName, "write", 5) == 0)
        genWrite(targetFile, symbolTable, exprNode);
    else
        genFuncCall(targetFile, symbolTable, exprNode);
}

// return stmt
void genReturnStmt(FILE* targetFile, STT* symbolTable, AST_NODE* returnNode, char* funcName){
    
    // genExpr
    genExpr(targetFile, symbolTable, returnNode->child);

    SymbolTableEntry* funcEntry = lookupSymbol(symbolTable, funcName);
    DATA_TYPE returnType = funcEntry->type->primitiveType;

    if(returnType == INT_TYPE){
        int retRegNum = getExprNodeReg(targetFile, returnNode->child);
        fprintf(targetFile, "move $%s, $%d\n", INT_RETURN_REG, retRegNum);
        releaseReg(GR.regManager, retRegNum);
    }
    else if(returnType == FLOAT_TYPE){
        int retRegNum = getExprNodeReg(targetFile, returnNode->child);
        fprintf(targetFile, "mov.s $%s, $f%d\n", FLOAT_RETURN_REG, retRegNum);
        releaseReg(GR.FPRegManager, retRegNum);
    }

    fprintf(targetFile, "j _end_%s\n", funcName);
}

void genAssignmentStmt(FILE* targetFile, STT* symbolTable, AST_NODE* assignmentNode){
    /* code generation for assignment node */
    /* lvalue */
    AST_NODE* lvalueNode = assignmentNode->child;
    genExpr(targetFile, symbolTable, lvalueNode);

    char *lvalueName = lvalueNode->semantic_value.identifierSemanticValue.identifierName;
    int lvalueLevel;
    SymbolTableEntry *lvalueEntry = lookupSymbolWithLevel(symbolTable, lvalueName, &lvalueLevel);
    int lvalueScope = LOCAL;
    if(lvalueLevel == 0)
        lvalueScope = GLOBAL;

    DATA_TYPE lvalueType = lvalueEntry->type->primitiveType;
    /* rvalue */
    AST_NODE* rvalueNode = assignmentNode->child->rightSibling;
    genExpr(targetFile, symbolTable, rvalueNode); // 0 means not function parameter
    DATA_TYPE rvalueType = getTypeOfExpr(symbolTable, rvalueNode);
    int rvalueRegNum = getExprNodeReg(targetFile, rvalueNode);
    /* type conversion */
    if(lvalueType == INT_TYPE && rvalueType == FLOAT_TYPE){
        int intRegNum = getReg(GR.regManager, targetFile);
        genFloatToInt(targetFile, intRegNum, rvalueRegNum);
        releaseReg(GR.FPRegManager, rvalueRegNum);
        rvalueRegNum = intRegNum;

        setPlaceOfASTNodeToReg(rvalueNode, INT_TYPE, intRegNum);
        useReg(GR.regManager, intRegNum, rvalueNode);
    }
    else if(lvalueType == FLOAT_TYPE && rvalueType == INT_TYPE){
        int floatRegNum = getReg(GR.FPRegManager, targetFile);
        genIntToFloat(targetFile, floatRegNum, rvalueRegNum);
        releaseReg(GR.regManager, rvalueRegNum);
        rvalueRegNum = floatRegNum;

        setPlaceOfASTNodeToReg(rvalueNode, FLOAT_TYPE, floatRegNum);
        useReg(GR.FPRegManager, floatRegNum, rvalueNode);
    }

    /* assignment */
    ExpValPlace* lvaluePlace = &(lvalueNode->valPlace);
    if(lvalueType == INT_TYPE){
        if(lvaluePlace->kind == STACK_TYPE)
            fprintf(targetFile, "sw $%d, -%d($fp)\n", rvalueRegNum, lvaluePlace->place.stackOffset);
        else if(lvaluePlace->kind == GLOBAL_TYPE)
            fprintf(targetFile, "sw $%d, %s+%d\n", rvalueRegNum, 
              lvaluePlace->place.data.label, lvaluePlace->place.data.offset);
        releaseReg(GR.regManager, rvalueRegNum);
    }
    if(lvalueType == FLOAT_TYPE){
        if(lvaluePlace->kind == STACK_TYPE)
            fprintf(targetFile, "s.s $%d, -%d($fp)\n", rvalueRegNum, lvaluePlace->place.stackOffset);
        else if(lvaluePlace->kind == GLOBAL_TYPE)
            fprintf(targetFile, "s.s $f%d, %s+%d\n", rvalueRegNum,
              lvaluePlace->place.data.label, lvaluePlace->place.data.offset);
        releaseReg(GR.FPRegManager, rvalueRegNum);
    }
}

void genExpr(FILE* targetFile, STT* symbolTable, AST_NODE* exprNode){
    /* code generation for expression */
    if( exprNode->nodeType == CONST_VALUE_NODE ){
        if( exprNode->semantic_value.const1->const_type == INTEGERC){
            int value = exprNode->semantic_value.const1->const_u.intval;
            int intRegNum = getReg(GR.regManager, targetFile);
            fprintf(targetFile, "li $%d, %d\n", intRegNum, value);

            setPlaceOfASTNodeToReg(exprNode, INT_TYPE, intRegNum);
            useReg(GR.regManager, intRegNum, exprNode);
        }
        else if ( exprNode->semantic_value.const1->const_type == FLOATC ){
            float value = exprNode->semantic_value.const1->const_u.fval;
            int floatRegNum = getReg(GR.FPRegManager, targetFile);
            fprintf(targetFile, "li.s $f%d, %f\n", floatRegNum, value);

            setPlaceOfASTNodeToReg(exprNode, FLOAT_TYPE, floatRegNum);
            useReg(GR.FPRegManager, floatRegNum, exprNode);
        }    
    }
    else if( exprNode->semantic_value.stmtSemanticValue.kind == FUNCTION_CALL_STMT ){
        char* callingFuncName = exprNode->child->semantic_value.identifierSemanticValue.identifierName;
        if(strncmp(callingFuncName, "read", 4) == 0){
            genRead(targetFile);
            genProcessIntReturnValue(targetFile, exprNode);
        }
        else if(strncmp(callingFuncName, "fread", 5) == 0){
            genFRead(targetFile);
            genProcessFloatReturnValue(targetFile, exprNode);
        }
        else if(strncmp(callingFuncName, "write", 5) == 0)
            genWrite(targetFile, symbolTable, exprNode);
        else{
            genFuncCall(targetFile, symbolTable, exprNode);
            genProcessFuncReturnValue(targetFile, symbolTable, exprNode);
        }
    }
    else if( exprNode->nodeType == IDENTIFIER_NODE ){
        char* name = exprNode->semantic_value.identifierSemanticValue.identifierName;
        int level, scope = LOCAL;
        SymbolTableEntry* entry = lookupSymbolWithLevel(symbolTable, name, &level);
        if(level == 0)
            scope = GLOBAL;

        DATA_TYPE type = entry->type->primitiveType;
        int arrayOffset = computeArrayOffset(entry, exprNode);

        if(scope == LOCAL){
            if(type == INT_TYPE)
                setPlaceOfASTNodeToStack(exprNode, INT_TYPE, entry->stackOffset + arrayOffset);
            else if(type == FLOAT_TYPE)
                setPlaceOfASTNodeToStack(exprNode, FLOAT_TYPE, entry->stackOffset + arrayOffset);
        }

        if(scope == GLOBAL){
            if(type == INT_TYPE)
                setPlaceOfASTNodeToGlobalData(exprNode, INT_TYPE, entry->name, arrayOffset);
            else if(type == FLOAT_TYPE)
                setPlaceOfASTNodeToGlobalData(exprNode, FLOAT_TYPE, entry->name, arrayOffset);
        }
    }
    else if( exprNode->nodeType == EXPR_NODE ){
        if( exprNode->semantic_value.exprSemanticValue.kind == UNARY_OPERATION ){
            genExpr(targetFile, symbolTable, exprNode->child);
            DATA_TYPE type = getTypeOfExpr(symbolTable, exprNode->child);
            int childRegNum = getExprNodeReg(targetFile, exprNode->child);

            UNARY_OPERATOR op = exprNode->semantic_value.exprSemanticValue.op.unaryOp;
            if(type == INT_TYPE){
                int regNum = getReg(GR.regManager, targetFile);

                genIntUnaryOpInstr(targetFile, op, regNum, childRegNum);

                setPlaceOfASTNodeToReg(exprNode, INT_TYPE, regNum);
                useReg(GR.regManager, regNum, exprNode);
                releaseReg(GR.regManager, childRegNum);
            }
            else if(type == FLOAT_TYPE){
                int regNum = getReg(GR.FPRegManager, targetFile);

                genFloatUnaryOpInstr(targetFile, op, regNum, childRegNum);

                setPlaceOfASTNodeToReg(exprNode, FLOAT_TYPE, regNum);
                useReg(GR.FPRegManager, regNum, exprNode);
                releaseReg(GR.FPRegManager, childRegNum);
            }
        }
        else if( exprNode->semantic_value.exprSemanticValue.kind == BINARY_OPERATION ){
            genExpr(targetFile, symbolTable, exprNode->child);
            genExpr(targetFile, symbolTable, exprNode->child->rightSibling);
            DATA_TYPE type = getTypeOfExpr(symbolTable, exprNode->child);
            int child1RegNum = getExprNodeReg(targetFile, exprNode->child);
            int child2RegNum = getExprNodeReg(targetFile, exprNode->child->rightSibling);

            BINARY_OPERATOR op = exprNode->semantic_value.exprSemanticValue.op.binaryOp;
            if(type == INT_TYPE){
                int regNum = getReg(GR.regManager, targetFile);

                genIntBinaryOpInstr(targetFile, op, regNum, child1RegNum, child2RegNum);

                setPlaceOfASTNodeToReg(exprNode, INT_TYPE, regNum);
                useReg(GR.regManager, regNum, exprNode);
                releaseReg(GR.regManager, child1RegNum);
                releaseReg(GR.regManager, child2RegNum);
            }
            else if(type == FLOAT_TYPE){
                int regNum;

                switch(op){
                    case BINARY_OP_ADD: case BINARY_OP_SUB: case BINARY_OP_MUL:
                    case BINARY_OP_DIV:

                        regNum = getReg(GR.FPRegManager, targetFile);
                        genFloatBinaryArithOpInstr(targetFile, op, regNum, child1RegNum, child2RegNum);
                        setPlaceOfASTNodeToReg(exprNode, FLOAT_TYPE, regNum);
                        useReg(GR.FPRegManager, regNum, exprNode);
                        break;

                    case BINARY_OP_EQ: case BINARY_OP_GE: case BINARY_OP_LE:
                    case BINARY_OP_NE: case BINARY_OP_GT: case BINARY_OP_LT: 

                        regNum = getReg(GR.regManager, targetFile);
                        genFloatBinaryRelaOpInstr(targetFile, op, regNum, child1RegNum, child2RegNum);
                        setPlaceOfASTNodeToReg(exprNode, INT_TYPE, regNum);
                        useReg(GR.FPRegManager, regNum, exprNode);
                        break;

                    case BINARY_OP_AND: case BINARY_OP_OR: 

                        assert(0);
                }
                releaseReg(GR.FPRegManager, child1RegNum);
                releaseReg(GR.FPRegManager, child2RegNum);
            }
        }
    }
}

void genAssignExpr(FILE* targetFile, STT* symbolTable, AST_NODE* exprNode){
    /* test, assign_expr(grammar): checkAssignmentStmt or checkExpr */
    if(exprNode->nodeType == STMT_NODE){
        if(exprNode->semantic_value.stmtSemanticValue.kind == ASSIGN_STMT)
            genAssignmentStmt(targetFile, symbolTable, exprNode);
    }
    else 
        genExpr(targetFile, symbolTable, exprNode);
}

void genFuncCall(FILE* targetFile, STT* symbolTable, AST_NODE* funcCallNode){
    /* codegen for jumping to the function(label)
     * HW6 Extension: with Parameter function call */
     char *funcName = funcCallNode->child->semantic_value.identifierSemanticValue.identifierName;
     fprintf(targetFile, "j %s\n",funcName);
}

void genProcessFuncReturnValue(FILE* targetFile, STT* symbolTable, AST_NODE* exprNode){
    /* after function call, process function return value, and store in exprNode->valPlace */
    AST_NODE* funcNameNode = exprNode->child;
    char* funcName = funcNameNode->semantic_value.identifierSemanticValue.identifierName;
    SymbolTableEntry* funcEntry = lookupSymbol(symbolTable, funcName);
    DATA_TYPE returnType = funcEntry->type->primitiveType;

    if(returnType == INT_TYPE)
        genProcessIntReturnValue(targetFile, exprNode);
    else if(returnType == FLOAT_TYPE)
        genProcessFloatReturnValue(targetFile, exprNode);
}

void genProcessIntReturnValue(FILE* targetFile, AST_NODE* exprNode){
    int intRegNum = getReg(GR.regManager, targetFile);
    fprintf(targetFile, "add $%d, $%s, $0\n", intRegNum, INT_RETURN_REG); /* equal to move */

    setPlaceOfASTNodeToReg(exprNode, INT_TYPE, intRegNum);
    useReg(GR.regManager, intRegNum, exprNode);
}

void genProcessFloatReturnValue(FILE* targetFile, AST_NODE* exprNode){
    int floatRegNum = getReg(GR.FPRegManager, targetFile);
    fprintf(targetFile, "mov.s $f%d, $%s\n", floatRegNum, FLOAT_RETURN_REG);

    setPlaceOfASTNodeToReg(exprNode, FLOAT_TYPE, floatRegNum);
    useReg(GR.FPRegManager, floatRegNum, exprNode);
}

int getExprNodeReg(FILE* targetFile, AST_NODE* exprNode){
    /* return register or FP register of expression value.
     * For value in memory, load it to register */
    if(exprNode->valPlace.kind == REG_TYPE){
        return exprNode->valPlace.place.regNum;
    }
    else if(exprNode->valPlace.kind == STACK_TYPE){
        int stackOffset = exprNode->valPlace.place.stackOffset;
        if(exprNode->valPlace.dataType == INT_TYPE){
            int regNum = getReg(GR.regManager, targetFile);
            fprintf(targetFile, "lw $%d, -%d($fp)\n", regNum, stackOffset);
            useReg(GR.regManager, regNum, exprNode);
            setPlaceOfASTNodeToReg(exprNode, INT_TYPE, regNum);
            return regNum;
        }
        else if(exprNode->valPlace.dataType == FLOAT_TYPE){
            int regNum = getReg(GR.FPRegManager, targetFile);
            fprintf(targetFile, "l.s $f%d, -%d($fp)\n", regNum, stackOffset);
            useReg(GR.FPRegManager, regNum, exprNode);
            setPlaceOfASTNodeToReg(exprNode, FLOAT_TYPE, regNum);
            return regNum;
        }
    }
    else if(exprNode->valPlace.kind == GLOBAL_TYPE){
        ExpValPlace* place = &(exprNode->valPlace);
        if(place->dataType == INT_TYPE){
            int regNum = getReg(GR.regManager, targetFile);
            fprintf(targetFile, "lw $%d, %s+%d\n", regNum, place->place.data.label, place->place.data.offset);
            useReg(GR.regManager, regNum, exprNode);
            setPlaceOfASTNodeToReg(exprNode, INT_TYPE, regNum);
            return regNum;
        }
        else if(place->dataType == FLOAT_TYPE){
            int regNum = getReg(GR.FPRegManager, targetFile);
            fprintf(targetFile, "l.s $f%d, %s+%d\n", regNum, place->place.data.label, place->place.data.offset);
            useReg(GR.FPRegManager, regNum, exprNode);
            setPlaceOfASTNodeToReg(exprNode, FLOAT_TYPE, regNum);
            return regNum;
        }
    }

    return -1;
}

int computeArrayOffset(SymbolTableEntry* symbolEntry, AST_NODE* usedNode){
    /* compute used Node's array offset */
    int arrayOffset = 0;
    int offsetOfEachDimension[MAX_ARRAY_DIMENSION] = {0};

    int dimension = symbolEntry->type->dimension - 1;
    offsetOfEachDimension[dimension] = 4;
    while(dimension > 0){
        offsetOfEachDimension[dimension - 1] = offsetOfEachDimension[dimension];
        offsetOfEachDimension[dimension - 1] *= symbolEntry->type->sizeOfEachDimension[dimension];
        dimension--;
    }

    dimension = symbolEntry->type->dimension;
    int i;
    AST_NODE* dimenChild = usedNode->child;
    for(i = 0; i < dimension; i++){
        /* array */
        arrayOffset += dimenChild->semantic_value.const1->const_u.intval * offsetOfEachDimension[i];

        dimenChild = dimenChild->rightSibling;
    }

    return arrayOffset;
}

/*** Data Resourse, RegisterManager Implementation ***/
void RMinit(RegisterManager* pThis, int numOfReg, int firstRegNum){
    pThis->numOfReg = numOfReg;
    pThis->firstRegNum = firstRegNum;

    int i;
    for(i=0; i<pThis->numOfReg; i++){
        pThis->regFull[i] = 0;
        pThis->regUser[i] = NULL;
    }
    pThis->lastReg = 0;
}

int getReg(RegisterManager* pThis, FILE* targetFile){
    /* get empty register to use, return register Number (16 ~ 23 for s0 ~ s7, r16 ~ r23 ) */

    /* find empty register first */
    int regIndex = findEmptyReg(pThis);
    if(regIndex != -1){
        pThis->regFull[regIndex] = 1;
        int regNum = regIndex + pThis->firstRegNum;
        return regNum;
    }
    
    /* if no empty register, spill one register out */
    regIndex = findEarlestUsedReg(pThis);
    spillReg(pThis, regIndex, targetFile);
    int regNum = regIndex + pThis->firstRegNum; // s0 = r16 in mips
    return regNum;
}

void useReg(RegisterManager* pThis, int regNum, AST_NODE* nodeUseThisReg){
    /* AST_NODE use register, build link from register Manager to AST_NODE */
    int regIndex = regNum - pThis->firstRegNum;
    pThis->regUser[regIndex] = nodeUseThisReg;
}

void releaseReg(RegisterManager* pThis, int regNum){
    /* release used register to free register.
     * break link from Register Manager to AST_NODE */
    int regIndex = regNum - pThis->firstRegNum;
    pThis->regFull[regIndex] = 0;
    pThis->regUser[regIndex] = NULL;
}

int findEmptyReg(RegisterManager* pThis){
    /* find the nearest(compare to lastReg) empty register.
     * if no empty register. return -1 */
    int index = (pThis->lastReg+1) % pThis->numOfReg;
    while(index != pThis->lastReg){
        if(!pThis->regFull[index]){
            pThis->lastReg = index;
            return index;
        }

        index = (index+1) % pThis->numOfReg;
    }
    return -1;
}

int findEarlestUsedReg(RegisterManager* pThis){
    /* find the earlest allocated register.
     * the function used when all register is full. */
    pThis->lastReg = (pThis->lastReg + 1) % pThis->numOfReg;
    return pThis->lastReg;
}

void spillReg(RegisterManager* pThis, int regIndex, FILE* targetFile){
    /* spill value of register to the runtime stack, then release this register */
    int regNum = regIndex + pThis->firstRegNum;
    if(pThis->regUser[regIndex]){
        /* if AST_NODE (register user) exist */
        ExpValPlace* place = &(pThis->regUser[regIndex]->valPlace);

        /* store value of register into stack */
        fprintf(targetFile, "sw $%d, -%d($fp)\n", regNum, GR.stackTop + 4);
        place->dataType = INT_TYPE;
        place->kind = STACK_TYPE;
        place->place.stackOffset = GR.stackTop + 4;
        GR.stackTop += 4;
    }

    releaseReg(pThis, regNum);
}

/*** Constant String Implementation ***/
void initConstStringSet(ConstStringSet* pThis){
    pThis->numOfConstString = 0;
}

void addConstString(ConstStringSet* pThis, int labelNum, char* string){
    pThis->constStrings[pThis->numOfConstString].labelNum = labelNum;
    pThis->constStrings[pThis->numOfConstString].string = string;
    pThis->numOfConstString++;
}

void genConstStrings(ConstStringSet* pThis, FILE* targetFile){
    int i;
    for(i=0; i<pThis->numOfConstString; i++){
        ConstStringPair* pair = &(pThis->constStrings[i]);
        fprintf(targetFile, "L%d: .asciiz %s\n", pair->labelNum, pair->string);
    }
}

/*** MIPS instruction generation ***/
void genIntUnaryOpInstr(FILE* targetFile, UNARY_OPERATOR op, int destRegNum, int srcRegNum){
    switch(op){
        case UNARY_OP_POSITIVE: genPosOpInstr(targetFile, destRegNum, srcRegNum); break;
        case UNARY_OP_NEGATIVE: genNegOpInstr(targetFile, destRegNum, srcRegNum); break;
        case UNARY_OP_LOGICAL_NEGATION: genNOTExpr(targetFile, destRegNum, srcRegNum); break;
    }
}

void genFloatUnaryOpInstr(FILE* targetFile, UNARY_OPERATOR op, int destRegNum, int srcRegNum){
    switch(op){
        case UNARY_OP_POSITIVE: genFPPosOpInstr(targetFile, destRegNum, srcRegNum); break;
        case UNARY_OP_NEGATIVE: genFPNegOpInstr(targetFile, destRegNum, srcRegNum); break;
        case UNARY_OP_LOGICAL_NEGATION: assert(0); break;
    }
}

void genIntBinaryOpInstr(FILE* targetFile, BINARY_OPERATOR op, 
  int destRegNum, int src1RegNum, int src2RegNum){
    switch(op){
        case BINARY_OP_ADD: genAddOpInstr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_SUB: genSubOpInstr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_MUL: genMulOpInstr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_DIV: genDivOpInstr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_EQ: genEQExpr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_GE: genGEExpr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_LE: genLEExpr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_NE: genNEExpr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_GT: genGTExpr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_LT: genLTExpr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_AND: genANDExpr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_OR: genORExpr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
    }
}

void genFloatBinaryArithOpInstr(FILE* targetFile, BINARY_OPERATOR op, 
  int destRegNum, int src1RegNum, int src2RegNum){
    switch(op){
        case BINARY_OP_ADD: genFPAddOpInstr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_SUB: genFPSubOpInstr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_MUL: genFPMulOpInstr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_DIV: genFPDivOpInstr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
    }
}

void genFloatBinaryRelaOpInstr(FILE* targetFile, BINARY_OPERATOR op, 
  int destRegNum, int src1RegNum, int src2RegNum){
    switch(op){
        case BINARY_OP_EQ: genFPEQInstr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_GE: genFPGEInstr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_LE: genFPLEInstr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_NE: genFPNEInstr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_GT: genFPGTInstr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
        case BINARY_OP_LT: genFPLTInstr(targetFile, destRegNum, src1RegNum, src2RegNum); break;
    }
}

void genAddOpInstr(FILE* targetFile, int destRegNum, int src1RegNum, int src2RegNum){
    fprintf(targetFile, "add $%d, $%d, $%d\n", destRegNum, src1RegNum, src2RegNum);
}

void genSubOpInstr(FILE* targetFile, int destRegNum, int src1RegNum, int src2RegNum){
    fprintf(targetFile, "sub $%d, $%d, $%d\n", destRegNum, src1RegNum, src2RegNum);
}

void genMulOpInstr(FILE* targetFile, int destRegNum, int src1RegNum, int src2RegNum){
    fprintf(targetFile, "mult $%d, $%d\n", src1RegNum, src2RegNum);
    fprintf(targetFile, "mflo $%d\n", destRegNum);
}

void genDivOpInstr(FILE* targetFile, int destRegNum, int src1RegNum, int src2RegNum){
    fprintf(targetFile, "div $%d, $%d\n", src1RegNum, src2RegNum);
    fprintf(targetFile, "mflo $%d\n", destRegNum);
}

void genEQExpr(FILE* targetFile, int destReg, int srcReg1, int srcReg2){
    fprintf(targetFile, "seq $%d, $%d, $%d\n", destReg, srcReg1, srcReg2);
}

void genNEExpr(FILE* targetFile, int destReg, int srcReg1, int srcReg2){
    fprintf(targetFile, "sne $%d, $%d, $%d\n", destReg, srcReg1, srcReg2);
}

void genLTExpr(FILE* targetFile, int destReg, int srcReg1, int srcReg2){
    fprintf(targetFile, "slt $%d, $%d, $%d\n", destReg, srcReg1, srcReg2);
}

void genGTExpr(FILE* targetFile, int destReg, int srcReg1, int srcReg2){
    fprintf(targetFile, "sgt $%d, $%d, $%d\n", destReg, srcReg1, srcReg2);
}

void genLEExpr(FILE* targetFile, int destReg, int srcReg1, int srcReg2){
    fprintf(targetFile, "sle $%d, $%d, $%d\n", destReg, srcReg1, srcReg2);
}

void genGEExpr(FILE* targetFile, int destReg, int srcReg1, int srcReg2){
    fprintf(targetFile, "sge $%d, $%d, $%d\n", destReg, srcReg1, srcReg2);
}

void genANDExpr(FILE* targetFile, int destReg, int srcReg1, int srcReg2){
    fprintf(targetFile, "and $%d, $%d, $%d\n", destReg, srcReg1, srcReg2);
}

void genORExpr(FILE* targetFile, int destReg, int srcReg1, int srcReg2){
    fprintf(targetFile, "or $%d, $%d, $%d\n", destReg, srcReg1, srcReg2);
}

void genNOTExpr(FILE* targetFile, int destReg, int srcReg){
    fprintf(targetFile, "seq $%d, $%d, $%d\n", destReg, srcReg, 0);
}

void genPosOpInstr(FILE* targetFile, int destRegNum, int srcRegNum){
    fprintf(targetFile, "add $%d, $%d, $0\n", destRegNum, srcRegNum);
}

void genNegOpInstr(FILE* targetFile, int destRegNum, int srcRegNum){
    fprintf(targetFile, "sub $%d, $0, $%d\n", destRegNum, srcRegNum);
}

// floating arithmetic operation
void genFPAddOpInstr(FILE* targetFile, int destRegNum, int src1RegNum, int src2RegNum){
    fprintf(targetFile, "add.s $f%d, $f%d, $f%d\n", destRegNum, src1RegNum, src2RegNum);
}

void genFPSubOpInstr(FILE* targetFile, int destRegNum, int src1RegNum, int src2RegNum){
    fprintf(targetFile, "sub.s $f%d, $f%d, $f%d\n", destRegNum, src1RegNum, src2RegNum);
}

void genFPMulOpInstr(FILE* targetFile, int destRegNum, int src1RegNum, int src2RegNum){
    fprintf(targetFile, "mul.s $f%d, $f%d, $f%d\n", destRegNum, src1RegNum, src2RegNum);
}

void genFPDivOpInstr(FILE* targetFile, int destRegNum, int src1RegNum, int src2RegNum){
    fprintf(targetFile, "div.s $f%d, $f%d, $f%d\n", destRegNum, src1RegNum, src2RegNum);
}

void genFPEQInstr(FILE* targetFile, int destRegNum, int src1RegNum, int src2RegNum){
    int falseLabel = GR.labelCounter++;
    int exitLabel  = GR.labelCounter++;
    
    fprintf(targetFile, "c.eq.s $f%d, $f%d\n", src1RegNum, src2RegNum);
    fprintf(targetFile, "bc1f L%d\n", falseLabel);
    fprintf(targetFile, "li $%d, 1\n", destRegNum);
    fprintf(targetFile, "j L%d\n", exitLabel);
    fprintf(targetFile, "L%d:\n", falseLabel);
    fprintf(targetFile, "li $%d, 0\n", destRegNum);
    fprintf(targetFile, "L%d:\n", exitLabel);
}

void genFPNEInstr(FILE* targetFile, int destRegNum, int src1RegNum, int src2RegNum){
    int falseLabel = GR.labelCounter++;
    int exitLabel  = GR.labelCounter++;
    
    fprintf(targetFile, "c.eq.s $f%d, $f%d\n", src1RegNum, src2RegNum);
    fprintf(targetFile, "bc1f L%d\n", falseLabel);
    fprintf(targetFile, "li $%d, 0\n", destRegNum);
    fprintf(targetFile, "j L%d\n", exitLabel);
    fprintf(targetFile, "L%d:\n", falseLabel);
    fprintf(targetFile, "li $%d, 1\n", destRegNum);
    fprintf(targetFile, "L%d:\n", exitLabel);
}

void genFPLTInstr(FILE* targetFile, int destRegNum, int src1RegNum, int src2RegNum){
    int falseLabel = GR.labelCounter++;
    int exitLabel  = GR.labelCounter++;
    
    fprintf(targetFile, "c.lt.s $f%d, $f%d\n", src1RegNum, src2RegNum);
    fprintf(targetFile, "bc1f L%d\n", falseLabel);
    fprintf(targetFile, "li $%d, 1\n", destRegNum);
    fprintf(targetFile, "j L%d\n", exitLabel);
    fprintf(targetFile, "L%d:\n", falseLabel);
    fprintf(targetFile, "li $%d, 0\n", destRegNum);
    fprintf(targetFile, "L%d:\n", exitLabel);
}

void genFPGTInstr(FILE* targetFile, int destRegNum, int src1RegNum, int src2RegNum){
    int falseLabel = GR.labelCounter++;
    int exitLabel  = GR.labelCounter++;
    
    fprintf(targetFile, "c.le.s $f%d, $f%d\n", src1RegNum, src2RegNum);
    fprintf(targetFile, "bc1f L%d\n", falseLabel);
    fprintf(targetFile, "li $%d, 0\n", destRegNum);
    fprintf(targetFile, "j L%d\n", exitLabel);
    fprintf(targetFile, "L%d:\n", falseLabel);
    fprintf(targetFile, "li $%d, 1\n", destRegNum);
    fprintf(targetFile, "L%d:\n", exitLabel);
}

void genFPGEInstr(FILE* targetFile, int destRegNum, int src1RegNum, int src2RegNum){
    int falseLabel = GR.labelCounter++;
    int exitLabel  = GR.labelCounter++;
    
    fprintf(targetFile, "c.lt.s $f%d, $f%d\n", src1RegNum, src2RegNum);
    fprintf(targetFile, "bc1f L%d\n", falseLabel);
    fprintf(targetFile, "li $%d, 0\n", destRegNum);
    fprintf(targetFile, "j L%d\n", exitLabel);
    fprintf(targetFile, "L%d:\n", falseLabel);
    fprintf(targetFile, "li $%d, 1\n", destRegNum);
    fprintf(targetFile, "L%d:\n", exitLabel);
}

void genFPLEInstr(FILE* targetFile, int destRegNum, int src1RegNum, int src2RegNum){
    int falseLabel = GR.labelCounter++;
    int exitLabel  = GR.labelCounter++;
    
    fprintf(targetFile, "c.le.s $f%d, $f%d\n", src1RegNum, src2RegNum);
    fprintf(targetFile, "bc1f L%d\n", falseLabel);
    fprintf(targetFile, "li $%d, 1\n", destRegNum);
    fprintf(targetFile, "j L%d\n", exitLabel);
    fprintf(targetFile, "L%d:\n", falseLabel);
    fprintf(targetFile, "li $%d, 0\n", destRegNum);
    fprintf(targetFile, "L%d:\n", exitLabel);
}

void genFPPosOpInstr(FILE* targetFile, int destRegNum, int srcRegNum){
    fprintf(targetFile, "mov.s $f%d, $f%d\n", destRegNum, srcRegNum);
}

void genFPNegOpInstr(FILE* targetFile, int destRegNum, int srcRegNum){
    fprintf(targetFile, "neg.s $f%d, $f%d\n", destRegNum, srcRegNum);
}

// casting
void genFloatToInt(FILE* targetFile, int destRegNum, int floatRegNum){
    fprintf(targetFile, "cvt.w.s $f%d, $f%d\n", floatRegNum, floatRegNum);
    fprintf(targetFile, "mfc1 $%d, $f%d\n", destRegNum, floatRegNum);
}

void genIntToFloat(FILE* targetFile, int destRegNum, int intRegNum){
    fprintf(targetFile, "mtc1 $%d, $f%d\n", intRegNum, destRegNum);
    fprintf(targetFile, "cvt.s.w $f%d, $f%d\n", destRegNum, destRegNum);
}

/* IO system call */
void genRead(FILE *targetFile){
    
    fprintf(targetFile, "li $v0 5\n");//syscall 5 means read_int;
    fprintf(targetFile, "syscall\n"); //the returned result will be in $v0
}

void genFRead(FILE *targetFile){
    
    fprintf(targetFile, "li $v0 6\n");//syscall 6 means read_float;
    fprintf(targetFile, "syscall\n"); //the returned result will be in $f0
}


void genWrite(FILE *targetFile, STT* symbolTable, AST_NODE* funcCallNode){
    
    // genExpr
    AST_NODE* ExprNode = funcCallNode->child->rightSibling->child;
    genExpr(targetFile, symbolTable, ExprNode);

    // check type to be printed
    if( ExprNode->nodeType == CONST_VALUE_NODE && 
      ExprNode->semantic_value.const1->const_type == STRINGC ){
        
        char *constString = ExprNode->semantic_value.const1->const_u.sc;
        int constStringLabel = GR.labelCounter++;
        addConstString(GR.constStrings, constStringLabel, constString);
        fprintf(targetFile, "li $v0, 4\n");
        fprintf(targetFile, "la $a0 L%d\n", constStringLabel);
        fprintf(targetFile, "syscall\n");
    }
    else{

        DATA_TYPE dataType = getTypeOfExpr(symbolTable, ExprNode);

        if(dataType == INT_TYPE){
            int intRegNum = getExprNodeReg(targetFile, ExprNode);
            fprintf(targetFile, "li $v0, 1\n");
            fprintf(targetFile, "move $a0, $%d\n", intRegNum);
            fprintf(targetFile, "syscall\n");
        }
        else if(dataType == FLOAT_TYPE){
            int floatRegNum = getExprNodeReg(targetFile, ExprNode);
            fprintf(targetFile, "li $v0, 2\n");
            fprintf(targetFile, "mov.s $f12, $f%d\n", floatRegNum);
            fprintf(targetFile, "syscall\n");
        }
    }

}
