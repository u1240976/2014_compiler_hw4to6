#include <stdio.h>
#include <assert.h>
#include "codeGen.h"
#include "header.h"
#include "symbolTable.h"
#include "semanticAnalysis.h"

#define GLOBAL 1
#define LOCAL 2
GlobalResource GR;

/* inner function prototype */
void _normalEval(FILE* targetFile, AST_NODE* childNode, int jumpLabel, int jumpCond);
/* jumpCond = TRUE_JUMP or FALSE_JUMP */
#define TRUE_JUMP 1
#define FALSE_JUMP 0
void _genParaList(FILE* targetFile, STT* symbolTable, AST_NODE* paraNode, ParameterNode* thisParameter);
int isAllConstIndex(AST_NODE* headNode);

/* function definition */
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
            setPlaceOfSymTableToGlobalData(entry, entry->name, 0);

            if(type->dimension != 0)
                fprintf(targetFile, "%s: .space %d\n", entry->name, varSize);
            else if(type->primitiveType == INT_TYPE){
                int constValue = 0; // no initialzaton -> initialize to 0
                if( variableNode->child ) // need to initialize
                    constValue = variableNode->child->semantic_value.const1->const_u.intval;

                fprintf(targetFile, "%s: .word %d\n", entry->name, constValue);
            }
            else if(type->primitiveType == FLOAT_TYPE){
                float constValue = 0.0; // no initialzaton -> initialize to 0
                if( variableNode->child ) // need to initialize
                    constValue = variableNode->child->semantic_value.const1->const_u.fval;

                fprintf(targetFile, "%s: .float %f\n", entry->name, constValue);
            }
        }
        else if(kind == LOCAL){
            GR.stackTop += varSize;
            setPlaceOfSymTableToStack(entry, GR.stackTop);

            // check if initialization required
            if( variableNode->semantic_value.identifierSemanticValue.kind == WITH_INIT_ID ){ // need to initialize
                
                if( type->primitiveType == INT_TYPE ){
                    
                    int intRegNum = getReg(GR.regManager, targetFile);
                    int constValue = variableNode->child->semantic_value.const1->const_u.intval;
                    fprintf(targetFile, "li $%d, %d\n", intRegNum, constValue);
                    fprintf(targetFile, "sw $%d, %d($fp)\n", intRegNum, -1*GR.stackTop); // initialize to stack.
                    releaseReg(GR.regManager, intRegNum);
                }
                else if( type->primitiveType == FLOAT_TYPE ){
                    
                    int floatRegNum = getReg(GR.FPRegManager, targetFile);
                    float constValue = variableNode->child->semantic_value.const1->const_u.fval;
                    fprintf(targetFile, "li.s $f%d, %f\n", floatRegNum, constValue);
                    fprintf(targetFile, "s.s $f%d, %d($fp)\n", floatRegNum, -1*GR.stackTop); // initialize to stack.
                    releaseReg(GR.FPRegManager, floatRegNum);
                }
            }
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

    /* into block: openScope & prologue
     *             processing Decl_list + Stmt_list
     *             epilogue & closeScope
     */

    openScope(symbolTable, USE, NULL);
    /* set parameters' place 
     * the 1st arg is fp+8, 2nd is fp+12 ... 
     * stackOffset = -8, -12 ... etc
     */
    setParaListStackOffset(symbolTable, paraListNode);
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

void setParaListStackOffset(STT* symbolTable, AST_NODE* paraListNode){
    /* set parameters' place in symbol table
     * the 1st arg is fp+8, 2nd is fp+12 ... 
     * stackOffset = -8, -12 ... etc
     */
    AST_NODE* funcParaNode = paraListNode->child;
    int stackOffset = -8;
    while(funcParaNode){
        char* varName = funcParaNode->child->rightSibling->semantic_value.identifierSemanticValue.identifierName;
        SymbolTableEntry* varEntry = lookupSymbol(symbolTable, varName);

        if(varEntry->type->dimension == 0){
            setPlaceOfSymTableToStack(varEntry, stackOffset);
        }
        else{
            /* is array parameter, child isn't NULL */
            setPlaceOfSymTableToIndirectAddr(varEntry, -1*stackOffset, 0);
        }

        funcParaNode = funcParaNode->rightSibling;
        stackOffset -= 4;
    }
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
            case FOR_STMT: genForStmt(targetFile, symbolTable, stmtNode, funcName); break;
            case IF_STMT: genIfStmt(targetFile, symbolTable, stmtNode, funcName); break;
            case ASSIGN_STMT: genAssignmentStmt(targetFile, symbolTable, stmtNode); break;
            case FUNCTION_CALL_STMT: genFuncCallStmt(targetFile, symbolTable, stmtNode, funcName); break;
            case RETURN_STMT: genReturnStmt(targetFile, symbolTable, stmtNode, funcName); break;
        }
    }
}

void genBlock(FILE* targetFile, STT* symbolTable, AST_NODE* blockNode, char* funcName){
    openScope(symbolTable, USE, NULL);

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
    
    int thenLabel = GR.labelCounter++;
    int elseLabel = GR.labelCounter++;
    int exitLabel = GR.labelCounter++;
    // condition
    int isShortEval = genShortRelExpr(targetFile, symbolTable, ifStmtNode->child, thenLabel, elseLabel);
    
    if(!isShortEval){
        // jump to else if condition not match
        int regNum = getExprNodeReg(targetFile, ifStmtNode->child);
        fprintf(targetFile, "beqz $%d L%d\n", regNum, elseLabel);
        if(ifStmtNode->child->valPlace.dataType == INT_TYPE)
            releaseReg(GR.regManager, regNum);
        else if(ifStmtNode->child->valPlace.dataType == FLOAT_TYPE)
            releaseReg(GR.FPRegManager, regNum);
    }
    
    // then block
    fprintf(targetFile, "L%d:\n", thenLabel);
    genStmt(targetFile, symbolTable, ifStmtNode->child->rightSibling, funcName);
    
    // jump over else
    fprintf(targetFile, "j L%d\n", exitLabel);

    // else block
    fprintf(targetFile, "L%d:\n", elseLabel);
    if( ifStmtNode->child->rightSibling->rightSibling->nodeType != NUL_NODE )
        genStmt(targetFile, symbolTable, ifStmtNode->child->rightSibling->rightSibling, funcName);

    // exit
    fprintf(targetFile, "L%d:\n", exitLabel);
}

void genWhileStmt(FILE* targetFile, STT* symbolTable, AST_NODE* whileStmtNode, char* funcName){
    
    int testLabel = GR.labelCounter++;
    int whileStmtLabel = GR.labelCounter++;
    int exitLabel = GR.labelCounter++;

    // Test Label
    fprintf(targetFile, "L%d:\n", testLabel);
    
    // condition
    int isShortEval = genShortRelExpr(targetFile, symbolTable, whileStmtNode->child, whileStmtLabel, exitLabel);
    
    if(!isShortEval){
        // check condition
        int regNum = getExprNodeReg(targetFile, whileStmtNode->child);
        fprintf(targetFile, "beqz $%d L%d\n", regNum, exitLabel);
        if(whileStmtNode->child->valPlace.dataType == INT_TYPE)
            releaseReg(GR.regManager, regNum);
        else if(whileStmtNode->child->valPlace.dataType == FLOAT_TYPE)
            releaseReg(GR.FPRegManager, regNum);
    }
    
    // Stmt
    fprintf(targetFile, "L%d:\n", whileStmtLabel);
    genStmt(targetFile, symbolTable, whileStmtNode->child->rightSibling, funcName);

    // loop back
    fprintf(targetFile, "j L%d\n", testLabel);

    // exit
    fprintf(targetFile, "L%d:\n", exitLabel);
}

void genForStmt(FILE* targetFile, STT* symbolTable, AST_NODE* forStmtNode, char* funcName){
    
    // node initialization
    AST_NODE* assignNode = forStmtNode->child->child;
    AST_NODE* condNode   = forStmtNode->child->rightSibling->child;
    AST_NODE* incNode    = forStmtNode->child->rightSibling->rightSibling->child;
    AST_NODE* blockNode  = forStmtNode->child->rightSibling->rightSibling->rightSibling;

    // Label initialization
    int testLabel = GR.labelCounter++;
    int incLabel  = GR.labelCounter++;
    int bodyLabel = GR.labelCounter++;
    int exitLabel = GR.labelCounter++;

    // assign stmt
    while(assignNode){ // handle multiple assign stmt

        genAssignExpr(targetFile, symbolTable, assignNode);
        assignNode = assignNode->rightSibling;
    }

    // condition
    // UNFINISH: genShortRelExpr
    fprintf(targetFile, "L%d:\n", testLabel);
    
        // handle multiple condition expr, except for last one
    if(condNode){
        while(condNode->rightSibling){
            
            genAssignExpr(targetFile, symbolTable, condNode);
            condNode = condNode->rightSibling;
        }
            // last condition expr
        int isShortEval = genShortRelExpr(targetFile, symbolTable, condNode, bodyLabel, exitLabel);
        if(!isShortEval){
            fprintf(targetFile, "beqz $%d L%d\n", condNode->valPlace.place.regNum, exitLabel);
            fprintf(targetFile, "j L%d\n", bodyLabel);
        }
    }

    // increment stmt
    fprintf(targetFile, "L%d:\n", incLabel);

    while(incNode){ // handle multiple assign stmt

        genAssignExpr(targetFile, symbolTable, incNode);
        incNode = incNode->rightSibling;
    }

    fprintf(targetFile, "j L%d\n", testLabel);

    // body
    fprintf(targetFile, "L%d:\n", bodyLabel);
    genStmt(targetFile, symbolTable, blockNode, funcName);
    fprintf(targetFile, "j L%d\n", incLabel);

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
        if(returnType != returnNode->child->valPlace.dataType){
            /* expr is float, conversion to int type */
            int intRegNum = getReg(GR.regManager, targetFile);
            genFloatToInt(targetFile, intRegNum, retRegNum);
            releaseReg(GR.FPRegManager, retRegNum);
            retRegNum = intRegNum;
        }

        fprintf(targetFile, "move $%s, $%d\n", INT_RETURN_REG, retRegNum);
        releaseReg(GR.regManager, retRegNum);
    }
    else if(returnType == FLOAT_TYPE){
        int retRegNum = getExprNodeReg(targetFile, returnNode->child);
        if(returnType != returnNode->child->valPlace.dataType){
            /* expr is int, conversion to float type */
            int floatRegNum = getReg(GR.FPRegManager, targetFile); 
            genIntToFloat(targetFile, floatRegNum, retRegNum);
            releaseReg(GR.regManager, retRegNum);
            retRegNum = floatRegNum;
        }

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
        /* lvalue = rvalue */
        if(lvaluePlace->kind == STACK_TYPE && lvaluePlace->arrIdxKind == STATIC_INDEX)
            fprintf(targetFile, "sw $%d, %d($fp)\n", rvalueRegNum, -1*lvaluePlace->place.stackOffset);
        else if(lvaluePlace->kind == STACK_TYPE && lvaluePlace->arrIdxKind == DYNAMIC_INDEX){
            int tempRegNum = getExprNodeReg(targetFile, lvalueNode->child);
            fprintf(targetFile, "add $%d, $%d, $fp\n", tempRegNum, tempRegNum);
            fprintf(targetFile, "sw $%d, %d($%d)\n", rvalueRegNum, 
              -1*lvaluePlace->place.stackOffset, tempRegNum);
            releaseReg(GR.regManager, tempRegNum);
        
        }
        else if(lvaluePlace->kind == GLOBAL_TYPE && lvaluePlace->arrIdxKind == STATIC_INDEX)
            fprintf(targetFile, "sw $%d, %s+%d\n", rvalueRegNum, 
              lvaluePlace->place.data.label, lvaluePlace->place.data.offset);
        else if(lvaluePlace->kind == GLOBAL_TYPE && lvaluePlace->arrIdxKind == DYNAMIC_INDEX){
            int tempRegNum = getExprNodeReg(targetFile, lvalueNode->child);
            fprintf(targetFile, "addi $%d, $%d, %s\n", tempRegNum, tempRegNum,
              lvalueNode->valPlace.place.data.label);
            fprintf(targetFile, "sw $%d, 0($%d)\n", rvalueRegNum, tempRegNum);
            releaseReg(GR.regManager, tempRegNum); 
        }
        else if(lvaluePlace->kind == INDIRECT_ADDRESS && lvaluePlace->arrIdxKind == STATIC_INDEX){
            int tempRegNum = getReg(GR.regManager, targetFile);
            fprintf(targetFile, "lw $%d, %d($fp)\n", tempRegNum, lvaluePlace->place.inAddr.offset1);
            fprintf(targetFile, "sw $%d, %d($%d)\n", rvalueRegNum, 
              lvaluePlace->place.inAddr.offset2, tempRegNum);
            releaseReg(GR.regManager, tempRegNum);
        }
        else if(lvaluePlace->kind == INDIRECT_ADDRESS && lvaluePlace->arrIdxKind == DYNAMIC_INDEX){
            int tempRegNum = getReg(GR.regManager, targetFile);
            fprintf(targetFile, "lw $%d, %d($fp)\n", tempRegNum, lvaluePlace->place.inAddr.offset1);

            int tempRegNum2 = getExprNodeReg(targetFile, lvalueNode->child);
            genAddOpInstr(targetFile, tempRegNum2, tempRegNum2, tempRegNum);
            releaseReg(GR.regManager, tempRegNum);

            fprintf(targetFile, "sw $%d, 0($%d)\n", rvalueRegNum, tempRegNum2);
            releaseReg(GR.regManager, tempRegNum2);
        }
        // no release, let ExprNode(=) use this register
        // releaseReg(GR.regManager, rvalueRegNum);
        /* return rvalue at ExprNode(=) */
        setPlaceOfASTNodeToReg(assignmentNode, INT_TYPE, rvalueRegNum);
        useReg(GR.regManager, rvalueRegNum, assignmentNode);
    }
    if(lvalueType == FLOAT_TYPE){
        /* lvalue = rvalue */
        if(lvaluePlace->kind == STACK_TYPE && lvaluePlace->arrIdxKind == STATIC_INDEX)
            fprintf(targetFile, "s.s $f%d, %d($fp)\n", rvalueRegNum, -1*lvaluePlace->place.stackOffset);
        else if(lvaluePlace->kind == STACK_TYPE && lvaluePlace->arrIdxKind == DYNAMIC_INDEX){
            int tempRegNum = getExprNodeReg(targetFile, lvalueNode->child);
            fprintf(targetFile, "add $%d, $%d, $fp\n", tempRegNum, tempRegNum);
            fprintf(targetFile, "s.s $f%d, %d($%d)\n", rvalueRegNum, 
              -1*lvaluePlace->place.stackOffset, tempRegNum);
            releaseReg(GR.regManager, tempRegNum);
        
        }
        else if(lvaluePlace->kind == GLOBAL_TYPE && lvaluePlace->arrIdxKind == STATIC_INDEX)
            fprintf(targetFile, "s.s $f%d, %s+%d\n", rvalueRegNum,
              lvaluePlace->place.data.label, lvaluePlace->place.data.offset);
        else if(lvaluePlace->kind == GLOBAL_TYPE && lvaluePlace->arrIdxKind == DYNAMIC_INDEX){
            int tempRegNum = getExprNodeReg(targetFile, lvalueNode->child);
            fprintf(targetFile, "addi $%d, $%d, %s\n", tempRegNum, tempRegNum, lvalueNode->valPlace.place.data.label);
            fprintf(targetFile, "s.s $f%d, 0($%d)\n", rvalueRegNum, tempRegNum);
            releaseReg(GR.regManager, tempRegNum); 
        }
        else if(lvaluePlace->kind == INDIRECT_ADDRESS && lvaluePlace->arrIdxKind == STATIC_INDEX){
            int tempRegNum = getReg(GR.regManager, targetFile);
            fprintf(targetFile, "lw $%d, %d($fp)\n", tempRegNum, lvaluePlace->place.inAddr.offset1);
            fprintf(targetFile, "s.s $f%d, %d($%d)\n", rvalueRegNum, 
              lvaluePlace->place.inAddr.offset2, tempRegNum);
            releaseReg(GR.regManager, tempRegNum);
        }
        else if(lvaluePlace->kind == INDIRECT_ADDRESS && lvaluePlace->arrIdxKind == DYNAMIC_INDEX){
            int tempRegNum = getReg(GR.regManager, targetFile);
            fprintf(targetFile, "lw $%d, %d($fp)\n", tempRegNum, lvaluePlace->place.inAddr.offset1);

            int tempRegNum2 = getExprNodeReg(targetFile, lvalueNode->child);
            genAddOpInstr(targetFile, tempRegNum2, tempRegNum2, tempRegNum);
            releaseReg(GR.regManager, tempRegNum);

            fprintf(targetFile, "s.s $f%d, 0($%d)\n", rvalueRegNum, tempRegNum);
            releaseReg(GR.regManager, tempRegNum);
        }

        // no release, let ExprNode(=) use this register
        // releaseReg(GR.FPRegManager, rvalueRegNum);
        /* return rvalue at ExprNode(=) */
        setPlaceOfASTNodeToReg(assignmentNode, FLOAT_TYPE, rvalueRegNum);
        useReg(GR.FPRegManager, rvalueRegNum, assignmentNode);
    }
}

void genExpr(FILE* targetFile, STT* symbolTable, AST_NODE* exprNode){
    /* code generation for expression */
    if( exprNode->nodeType == CONST_VALUE_NODE ){
        /* store const value in register.
         * set register number in AST_NODE's place.
         */
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
        /* assign lvalue(memory address) to identifier place (LOCAL is stackOffset, GLOBAL is (label, offset))
         * 1. find variable name
         * 2. find symbolTable entry
         * 3. compute array offset
         */
        char* name = exprNode->semantic_value.identifierSemanticValue.identifierName;
        int level, scope = LOCAL;
        SymbolTableEntry* entry = lookupSymbolWithLevel(symbolTable, name, &level);
        if(level == 0)
            scope = GLOBAL;

        DATA_TYPE type = entry->type->primitiveType;
        int arrayOffset = 0;
        ArrayIndexKind arrIdxKind = computeArrayOffset(targetFile, symbolTable, entry, exprNode, &arrayOffset);

        if(scope == LOCAL){
            if(entry->place.kind == INDIRECT_ADDRESS){
                int stackOffset = entry->place.place.inAddr.offset1;
                if(type == INT_TYPE && arrIdxKind == STATIC_INDEX)
                    setPlaceOfASTNodeToIndirectAddr(exprNode, INT_TYPE, stackOffset, arrayOffset, STATIC_INDEX);
                else if(type == INT_TYPE && arrIdxKind == DYNAMIC_INDEX)
                    setPlaceOfASTNodeToIndirectAddr(exprNode, INT_TYPE, stackOffset, 0, DYNAMIC_INDEX);
                else if(type == FLOAT_TYPE && arrIdxKind == STATIC_INDEX)
                    setPlaceOfASTNodeToIndirectAddr(exprNode, FLOAT_TYPE, stackOffset, arrayOffset, STATIC_INDEX);
                else if(type == FLOAT_TYPE && arrIdxKind == DYNAMIC_INDEX)
                    setPlaceOfASTNodeToIndirectAddr(exprNode, FLOAT_TYPE, stackOffset, 0, DYNAMIC_INDEX);
            
            }
            else{
                /* STACK_TYPE */
                int stackOffset = entry->place.place.stackOffset;
                if(type == INT_TYPE && arrIdxKind == STATIC_INDEX)
                    setPlaceOfASTNodeToStack(exprNode, INT_TYPE, stackOffset - arrayOffset, STATIC_INDEX);
                else if(type == INT_TYPE && arrIdxKind == DYNAMIC_INDEX)
                    setPlaceOfASTNodeToStack(exprNode, INT_TYPE, stackOffset, DYNAMIC_INDEX);
                else if(type == FLOAT_TYPE && arrIdxKind == STATIC_INDEX)
                    setPlaceOfASTNodeToStack(exprNode, FLOAT_TYPE, stackOffset - arrayOffset, STATIC_INDEX);
                else if(type == FLOAT_TYPE && arrIdxKind == DYNAMIC_INDEX)
                    setPlaceOfASTNodeToStack(exprNode, FLOAT_TYPE, stackOffset, DYNAMIC_INDEX);
            }
        }

        if(scope == GLOBAL){
            /* GLOBAL_TYPE */
            if(type == INT_TYPE && arrIdxKind == STATIC_INDEX)
                setPlaceOfASTNodeToGlobalData(exprNode, INT_TYPE, entry->name, arrayOffset, STATIC_INDEX);
            else if(type == INT_TYPE && arrIdxKind == DYNAMIC_INDEX)
                setPlaceOfASTNodeToGlobalData(exprNode, INT_TYPE, entry->name, 0, DYNAMIC_INDEX);
            else if(type == FLOAT_TYPE && arrIdxKind == STATIC_INDEX)
                setPlaceOfASTNodeToGlobalData(exprNode, FLOAT_TYPE, entry->name, arrayOffset, STATIC_INDEX);
            else if(type == FLOAT_TYPE && arrIdxKind == DYNAMIC_INDEX)
                setPlaceOfASTNodeToGlobalData(exprNode, FLOAT_TYPE, entry->name, 0, DYNAMIC_INDEX);
        }
    }
    else if( exprNode->nodeType == EXPR_NODE ){
        if( exprNode->semantic_value.exprSemanticValue.kind == UNARY_OPERATION ){
            /* exprNode = unary operator */
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
            /* exprNode = binary operator */
            genExpr(targetFile, symbolTable, exprNode->child);
            genExpr(targetFile, symbolTable, exprNode->child->rightSibling);

            /* implicit type conversion */
            DATA_TYPE type1 = getTypeOfExpr(symbolTable, exprNode->child);
            DATA_TYPE type2 = getTypeOfExpr(symbolTable, exprNode->child->rightSibling);
            assert(type1 == INT_TYPE || type1 == FLOAT_TYPE);
            assert(type2 == INT_TYPE || type2 == FLOAT_TYPE);
            DATA_TYPE type = type1; /* if type1 == type2, then type = type1 = type2 = (int or float) */
            int child1RegNum, child2RegNum;
            if(type1 != type2){
                /* INT op FLOAT => FLOAT op FLOAT */
                type = FLOAT_TYPE;
                if(type1 == INT_TYPE){
                    int child1OriRegNum = getExprNodeReg(targetFile, exprNode->child);
                    child1RegNum = getReg(GR.FPRegManager, targetFile);
                    genIntToFloat(targetFile, child1RegNum, child1OriRegNum);
                    releaseReg(GR.regManager, child1OriRegNum);

                    child2RegNum = getExprNodeReg(targetFile, exprNode->child->rightSibling);
                }
                else if(type2 == INT_TYPE){
                    int child2OriRegNum = getExprNodeReg(targetFile, exprNode->child->rightSibling);
                    child2RegNum = getReg(GR.FPRegManager, targetFile);
                    genIntToFloat(targetFile, child2RegNum, child2OriRegNum);
                    releaseReg(GR.FPRegManager, child2OriRegNum);

                    child1RegNum = getExprNodeReg(targetFile, exprNode->child);
                }
            }
            else{
                child1RegNum = getExprNodeReg(targetFile, exprNode->child);
                child2RegNum = getExprNodeReg(targetFile, exprNode->child->rightSibling);
            }

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
        if(exprNode->semantic_value.stmtSemanticValue.kind == ASSIGN_STMT){
            genAssignmentStmt(targetFile, symbolTable, exprNode);
            return;
        }
    }
    genExpr(targetFile, symbolTable, exprNode);
}

int genShortRelExpr(FILE* targetFile, STT* symbolTable, AST_NODE* exprNode, int trueLabel, int falseLabel){
    /* generate short circuit relational expression for IF/FOR/WHILE stmt's conditional expression 
     *
     * return 1 if using short circuit evaluation.
     * return 0 if using normal genAssignExpr.
     *
     * trueLabel means if exprNode is true, jump to true
     * falseLabel means if exprNode is false, jump to false
     *
     * and(true, false):
     *     j false if not exp1 ( genShortRelExpr(exp1, exp1True, false) )
     *     exp1True:
     *     j false if not exp2 ( genShortRelExpr(exp2, true, false) )
     *     j true
     *
     * or(true, false):
     *     j true if exp1 ( genShortRelExpr(exp1, true, exp1False) )
     *     exp1False:
     *     j true if exp2 ( genShortRelExpr(exp2, true, false) )
     *     j false
     */

    int isRelOp = 0;
    EXPR_KIND opKind;
    BINARY_OPERATOR binaryOp;
    if(exprNode->nodeType == EXPR_NODE){
        opKind = exprNode->semantic_value.exprSemanticValue.kind;

        if(opKind == BINARY_OPERATION){
            binaryOp = exprNode->semantic_value.exprSemanticValue.op.binaryOp;

            if(binaryOp == BINARY_OP_AND || binaryOp == BINARY_OP_OR)
                isRelOp = 1;
        }
    }

    if(isRelOp == 0){
        genAssignExpr(targetFile, symbolTable, exprNode);
        return 0;
    }
    else if(isRelOp == 1){
        if(binaryOp == BINARY_OP_AND){
            int child1TrueLabel = GR.labelCounter++;

            int isShortEval = genShortRelExpr(targetFile, symbolTable, 
              exprNode->child, child1TrueLabel, falseLabel);

            if(!isShortEval) /* j false if not exp1 */
                _normalEval(targetFile, exprNode->child, falseLabel, FALSE_JUMP);

            fprintf(targetFile, "L%d:\n", child1TrueLabel);

            isShortEval = genShortRelExpr(targetFile, symbolTable, 
              exprNode->child->rightSibling, trueLabel, falseLabel);

            if(!isShortEval) /* j false if not exp2 */
                _normalEval(targetFile, exprNode->child->rightSibling, falseLabel, FALSE_JUMP);

            fprintf(targetFile, "j L%d\n", trueLabel);
        }
        if(binaryOp == BINARY_OP_OR){
            int child1FalseLabel = GR.labelCounter++;

            int isShortEval = genShortRelExpr(targetFile, symbolTable, 
              exprNode->child, trueLabel, child1FalseLabel);

            if(!isShortEval) /* j true if exp1 */
                _normalEval(targetFile, exprNode->child, trueLabel, TRUE_JUMP);

            fprintf(targetFile, "L%d:\n", child1FalseLabel);

            isShortEval = genShortRelExpr(targetFile, symbolTable, 
              exprNode->child->rightSibling, trueLabel, falseLabel);

            if(!isShortEval) /* j true if exp2 */
                _normalEval(targetFile, exprNode->child->rightSibling, trueLabel, TRUE_JUMP);

            fprintf(targetFile, "j L%d\n", falseLabel);
        }
    }
}

void _normalEval(FILE* targetFile, AST_NODE* childNode, int jumpLabel, int jumpCond){
    int regNum = getExprNodeReg(targetFile, childNode);

    if(jumpCond == TRUE_JUMP)
        fprintf(targetFile, "bne $%d, $0, L%d\n", regNum, jumpLabel); /* j jumpLabel if exp1 */
    else if(jumpCond == FALSE_JUMP)
        fprintf(targetFile, "beqz $%d, L%d\n", regNum, jumpLabel); /* j jumpLabel if not exp1 */

    if(childNode->valPlace.dataType == INT_TYPE)
        releaseReg(GR.regManager, regNum);
    else if(childNode->valPlace.dataType == FLOAT_TYPE)
        releaseReg(GR.FPRegManager, regNum);
}

void genFuncCall(FILE* targetFile, STT* symbolTable, AST_NODE* funcCallNode){
    /* codegen for jumping to the function(label)
     * HW6 Extension: with Parameter function call */
     
    /* check if parameters exist
    if exist -> push into stack */
    int numOfPara = 0;
    AST_NODE* paraNode = funcCallNode->child->rightSibling;
    if( paraNode->nodeType != NUL_NODE ){ // parameters exist

        paraNode = paraNode->child;
        numOfPara = genParaList(targetFile, symbolTable, paraNode);
    }
    
    char *funcName = funcCallNode->child->semantic_value.identifierSemanticValue.identifierName;
    fprintf(targetFile, "jal %s\n",funcName);

    /* pop out all the parameter if exist */
    fprintf(targetFile, "addi $sp, $sp, %d\n", 4 * numOfPara);
}

int genParaList(FILE* targetFile, STT* symbolTable, AST_NODE* paraNode){
    AST_NODE* funcNameNode = paraNode->parent->leftmostSibling;
    char* funcName = funcNameNode->semantic_value.identifierSemanticValue.identifierName;
    SymbolTableEntry* funcEntry = lookupSymbol(symbolTable, funcName);
    ParameterNode* funcParaList = funcEntry->functionParameterList;

    int paraNum = countRightSibling(paraNode);
    _genParaList(targetFile, symbolTable, paraNode, funcParaList);
    return paraNum;
}

void _genParaList(FILE* targetFile, STT* symbolTable, AST_NODE* paraNode, 
  ParameterNode* thisParameter){

    // recursive call
    if( paraNode->rightSibling )
        _genParaList(targetFile, symbolTable, paraNode->rightSibling, thisParameter->next);
    
    // check if parameter is array
    int dimension = 0;
    if( paraNode->nodeType == IDENTIFIER_NODE ){
        
        char* varName = paraNode->semantic_value.identifierSemanticValue.identifierName;
        SymbolTableEntry* entry = lookupSymbol(symbolTable, varName);
        TypeDescriptor* type = entry->type;
        dimension = type->dimension;
    }

    if( dimension != 0 ){
        /* It is an array
           GLOBAL -> get offset(name)
           LOCAL  -> may access non-local array
                     non-local array address relatives to non-local fp 
         */

        char* varName = paraNode->semantic_value.identifierSemanticValue.identifierName;
        int level, scope = LOCAL;
        SymbolTableEntry* entry = lookupSymbolWithLevel(symbolTable, varName, &level);
        if(level == 0)
            scope = GLOBAL;
        
        /* passing array address in array parameter */
        int regNum = getReg(GR.regManager, targetFile);
        int arrayOffset = 0;
        ArrayIndexKind arrIdxKind = computeArrayOffset(targetFile, symbolTable, entry, paraNode, &arrayOffset);

        if(scope == GLOBAL){
            /* pass global array address in stack(for indirect address) */
            if(arrIdxKind == STATIC_INDEX){
                /* varName + arrayOffset */
                fprintf(targetFile, "li $%d, %s+%d\n", regNum, varName, arrayOffset);
                fprintf(targetFile, "sw $%d, 0($sp)\n", regNum);
            }
            else if(arrIdxKind == DYNAMIC_INDEX){
                /* varName + dynamic arrayOffset */
                int offsetRegNum = getExprNodeReg(targetFile, paraNode->child);
                fprintf(targetFile, "addi $%d, $%d, %s\n", regNum, offsetRegNum, varName);
                fprintf(targetFile, "sw $%d, 0($sp)\n", regNum);
                releaseReg(GR.regManager, offsetRegNum);
            }
        }
        else if(scope == LOCAL){
            if(entry->place.kind == STACK_TYPE){
                /* pass local array address in stack(for indirect address) */
                if(arrIdxKind == STATIC_INDEX){
                    int stackOffset = entry->place.place.stackOffset;
                    fprintf(targetFile, "addi $%d, $fp, %d\n", regNum, -1*stackOffset);
                    fprintf(targetFile, "addi $%d, $%d, %d\n", regNum, regNum, arrayOffset);
                    fprintf(targetFile, "sw $%d, 0($sp)\n", regNum);
                }
                else if(arrIdxKind == DYNAMIC_INDEX){
                    int stackOffset = entry->place.place.stackOffset;
                    fprintf(targetFile, "addi $%d, $fp, %d\n", regNum, -1*stackOffset);
                    int offsetRegNum = getExprNodeReg(targetFile, paraNode->child);
                    fprintf(targetFile, "add $%d, $%d, $%d\n", regNum, regNum, offsetRegNum);
                    fprintf(targetFile, "sw $%d, 0($sp)\n", regNum);
                    releaseReg(GR.regManager, offsetRegNum);
                }
            }
            else if(entry->place.kind == INDIRECT_ADDRESS){
                /* pass indirect address array address in stack(for indirect address) */
                if(arrIdxKind == STATIC_INDEX){
                    int stackOffset = entry->place.place.inAddr.offset1;
                    fprintf(targetFile, "lw $%d, %d($fp)\n", regNum, -1*stackOffset);
                    fprintf(targetFile, "addi $%d, $%d, %d\n", regNum, regNum, arrayOffset);
                    fprintf(targetFile, "sw $%d, 0($sp)\n", regNum);
                }
                else if(arrIdxKind == DYNAMIC_INDEX){
                    int stackOffset = entry->place.place.inAddr.offset1;
                    fprintf(targetFile, "lw $%d, %d($fp)\n", regNum, -1*stackOffset);
                    int offsetRegNum = getExprNodeReg(targetFile, paraNode->child);
                    fprintf(targetFile, "add $%d, $%d, $%d\n", regNum, regNum, offsetRegNum);
                    fprintf(targetFile, "sw $%d, 0($sp)\n", regNum);
                    releaseReg(GR.regManager, offsetRegNum);
                }
            }
         
        }

        releaseReg(GR.regManager, regNum);
    }
    else{ 
        genExpr(targetFile, symbolTable, paraNode);
        int regNum = getExprNodeReg(targetFile, paraNode);
        DATA_TYPE funcParaType = thisParameter->type->primitiveType;

        if( funcParaType == INT_TYPE ){
            if( paraNode->valPlace.dataType != funcParaType ){
                /* type conversion of parameter(float to int) */
                int intRegNum = getReg(GR.regManager, targetFile);
                genFloatToInt(targetFile, intRegNum, regNum);
                releaseReg(GR.FPRegManager, regNum);
                regNum = intRegNum;
            }

            fprintf(targetFile, "sw $%d, 0($sp)\n", regNum);
            releaseReg(GR.regManager, regNum);
        }
        else if( funcParaType == FLOAT_TYPE ){
            if( paraNode->valPlace.dataType != funcParaType ){
                /* type conversion of parameter(int to float) */
                int floatRegNum = getReg(GR.FPRegManager, targetFile);
                genIntToFloat(targetFile, floatRegNum, regNum);
                releaseReg(GR.regManager, regNum);
                regNum = floatRegNum;
            }

            fprintf(targetFile, "s.s $f%d, 0($sp)\n", regNum);
            releaseReg(GR.FPRegManager, regNum);
        }
    }
    //both int & float & array require 4 bytes
    fprintf(targetFile, "addi $sp, $sp, -4\n");
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
    /* return register or FP register of expression value(from exprNode->valPlace).
     * return -1 if AST_NODE doesn't have place.
     * For value in memory, load it to register */
    if(exprNode->valPlace.kind == REG_TYPE){
        return exprNode->valPlace.place.regNum;
    }
    else if(exprNode->valPlace.kind == STACK_TYPE){
        int stackOffset = exprNode->valPlace.place.stackOffset;
        if(exprNode->valPlace.dataType == INT_TYPE){
            int regNum = getReg(GR.regManager, targetFile);

            if(exprNode->valPlace.arrIdxKind == STATIC_INDEX)
                fprintf(targetFile, "lw $%d, %d($fp)\n", regNum, -1*stackOffset);
            else if(exprNode->valPlace.arrIdxKind == DYNAMIC_INDEX){
                int dyIndexRegNum = getExprNodeReg(targetFile, exprNode->child);
                fprintf(targetFile, "add $%d, $%d, $fp\n", dyIndexRegNum, dyIndexRegNum);
                fprintf(targetFile, "lw $%d, %d($%d)\n", regNum, -1*stackOffset, dyIndexRegNum);
                releaseReg(GR.regManager, dyIndexRegNum);
            }

            useReg(GR.regManager, regNum, exprNode);
            setPlaceOfASTNodeToReg(exprNode, INT_TYPE, regNum);
            return regNum;
        }
        else if(exprNode->valPlace.dataType == FLOAT_TYPE){
            int regNum = getReg(GR.FPRegManager, targetFile);

            if(exprNode->valPlace.arrIdxKind == STATIC_INDEX)
                fprintf(targetFile, "l.s $f%d, %d($fp)\n", regNum, -1*stackOffset);
            else if(exprNode->valPlace.arrIdxKind == DYNAMIC_INDEX){
                int dyIndexRegNum = getExprNodeReg(targetFile, exprNode->child);
                fprintf(targetFile, "add $%d, $%d, $fp\n", dyIndexRegNum, dyIndexRegNum);
                fprintf(targetFile, "l.s $f%d, %d($%d)\n", regNum, -1*stackOffset, dyIndexRegNum);
                releaseReg(GR.regManager, dyIndexRegNum);
            }

            useReg(GR.FPRegManager, regNum, exprNode);
            setPlaceOfASTNodeToReg(exprNode, FLOAT_TYPE, regNum);
            return regNum;
        }
    }
    else if(exprNode->valPlace.kind == GLOBAL_TYPE){
        ExpValPlace* place = &(exprNode->valPlace);
        if(place->dataType == INT_TYPE){
            int regNum = getReg(GR.regManager, targetFile);

            if(exprNode->valPlace.arrIdxKind == STATIC_INDEX)
                fprintf(targetFile, "lw $%d, %s+%d\n", regNum, place->place.data.label, place->place.data.offset);
            else if(exprNode->valPlace.arrIdxKind == DYNAMIC_INDEX){
                int dyIndexRegNum = getExprNodeReg(targetFile, exprNode->child); 
                fprintf(targetFile, "addi $%d, $%d, %s\n", dyIndexRegNum, dyIndexRegNum, place->place.data.label);
                fprintf(targetFile, "lw $%d, 0($%d)\n", regNum, dyIndexRegNum);
                releaseReg(GR.regManager, dyIndexRegNum);
            }

            useReg(GR.regManager, regNum, exprNode);
            setPlaceOfASTNodeToReg(exprNode, INT_TYPE, regNum);
            return regNum;
        }
        else if(place->dataType == FLOAT_TYPE){
            int regNum = getReg(GR.FPRegManager, targetFile);

            if(exprNode->valPlace.arrIdxKind == STATIC_INDEX)
                fprintf(targetFile, "l.s $f%d, %s+%d\n", regNum, place->place.data.label, place->place.data.offset);
            else if(exprNode->valPlace.arrIdxKind == DYNAMIC_INDEX){
                int dyIndexRegNum = getExprNodeReg(targetFile, exprNode->child); 
                fprintf(targetFile, "addi $%d, $%d, %s\n", dyIndexRegNum, dyIndexRegNum, place->place.data.label);
                fprintf(targetFile, "l.s $f%d, 0($%d)\n", regNum, dyIndexRegNum);
                releaseReg(GR.regManager, dyIndexRegNum);
            }

            useReg(GR.FPRegManager, regNum, exprNode);
            setPlaceOfASTNodeToReg(exprNode, FLOAT_TYPE, regNum);
            return regNum;
        }
    }
    else if(exprNode->valPlace.kind == INDIRECT_ADDRESS){
        int offset1 = exprNode->valPlace.place.inAddr.offset1;
        int offset2 = exprNode->valPlace.place.inAddr.offset2;
        int tempRegNum = getReg(GR.regManager, targetFile);
        fprintf(targetFile, "lw $%d, %d($fp)\n", tempRegNum, offset1);

        int regNum = 0;
        if(exprNode->valPlace.dataType == INT_TYPE){
            regNum = getReg(GR.regManager, targetFile);
            if(exprNode->valPlace.arrIdxKind == STATIC_INDEX)
                fprintf(targetFile, "lw $%d, %d($%d)\n", regNum, offset2, tempRegNum);
            else if(exprNode->valPlace.arrIdxKind == DYNAMIC_INDEX){
                int dyIndexRegNum = getExprNodeReg(targetFile, exprNode->child); 
                genAddOpInstr(targetFile, tempRegNum, tempRegNum, dyIndexRegNum);
                releaseReg(GR.regManager, dyIndexRegNum);
                fprintf(targetFile, "lw $%d, 0($%d)\n", regNum, tempRegNum);
            }
            useReg(GR.regManager, regNum, exprNode);
            setPlaceOfASTNodeToReg(exprNode, INT_TYPE, regNum);
        }
        else if(exprNode->valPlace.dataType == FLOAT_TYPE){
            regNum = getReg(GR.FPRegManager, targetFile);
            if(exprNode->valPlace.arrIdxKind == STATIC_INDEX)
                fprintf(targetFile, "l.s $f%d, %d($%d)\n", regNum, offset2, tempRegNum);
            else if(exprNode->valPlace.arrIdxKind == DYNAMIC_INDEX){
                int dyIndexRegNum = getExprNodeReg(targetFile, exprNode->child); 
                genAddOpInstr(targetFile, tempRegNum, tempRegNum, dyIndexRegNum);
                releaseReg(GR.regManager, dyIndexRegNum);
                fprintf(targetFile, "l.s $f%d, 0($%d)\n", regNum, tempRegNum);
            }
            useReg(GR.FPRegManager, regNum, exprNode);
            setPlaceOfASTNodeToReg(exprNode, FLOAT_TYPE, regNum);
        }

        releaseReg(GR.regManager, tempRegNum);

        return regNum;
    }

    return -1;
}

ArrayIndexKind computeArrayOffset(FILE* targetFile, STT* symbolTable, SymbolTableEntry* symbolEntry, 
  AST_NODE* usedNode, int* staticOffset){
    /* compute used Node's array offset, use symbol table type
     * example, a[5] for int a[10], offset = 5*sizeof(int) = 20
     * example, a[5][6] for int a[10][10], offset = (50+6)*sizeof(int) = 224
     */
    int arrayOffset = 0;
    int offsetOfEachDimension[MAX_ARRAY_DIMENSION] = {0};

    int dimension = symbolEntry->type->dimension - 1;

    if(dimension >= 0)
        offsetOfEachDimension[dimension] = 4;
    while(dimension > 0){
        offsetOfEachDimension[dimension - 1] = offsetOfEachDimension[dimension];
        offsetOfEachDimension[dimension - 1] *= symbolEntry->type->sizeOfEachDimension[dimension];
        dimension--;
    }

    dimension = symbolEntry->type->dimension;
    int i;
    AST_NODE* dimenChild = usedNode->child;
    AST_NODE* FirstChild = usedNode->child;

    if(isAllConstIndex(dimenChild)){
        for(i = 0; i < dimension; i++){
            if(!dimenChild)
                /* return array address, not scalar value */
                break;

            /* array */
            arrayOffset += dimenChild->semantic_value.const1->const_u.intval * offsetOfEachDimension[i];

            dimenChild = dimenChild->rightSibling;
        }
        *staticOffset = arrayOffset;
        return STATIC_INDEX;
    }
    else{
        /* dynamic array index, store in register attach on array first child(index)'s place. */ 
        int regNum = 0; 
        for(i = 0; i < dimension; i++){
        // regNum(arrayOffset) = sum( value of dimenChild * offsetOfEachDimension[i] for i in (0, dimension));

            if(!dimenChild)
                /* return array address, not scalar value */
                break;

            // regNum(arrayOffset) += value of dimenChild * offsetOfEachDimension[i];
            genExpr(targetFile, symbolTable, dimenChild);

            // constRegNum = offsetOfEachDimension[i]
            int constRegNum = getReg(GR.regManager, targetFile);
            fprintf(targetFile, "li $%d, %d\n", constRegNum, offsetOfEachDimension[i]);

            // childRegNum = value of dimenChild * constRegNum
            int childRegNum = getExprNodeReg(targetFile, dimenChild);
            genMulOpInstr(targetFile, childRegNum, childRegNum, constRegNum);
            releaseReg(GR.regManager, constRegNum);

            // regNum += childRegNum
            if(i == 0){
                regNum = getReg(GR.regManager, targetFile);
                fprintf(targetFile, "li $%d, 0\n", regNum);
            }
            else
                regNum = getExprNodeReg(targetFile, FirstChild);
            genAddOpInstr(targetFile, regNum, regNum, childRegNum);
            releaseReg(GR.regManager, childRegNum);

            // FirstChild use regNum;
            useReg(GR.regManager, regNum, FirstChild);
            setPlaceOfASTNodeToReg(FirstChild, INT_TYPE, regNum);

            dimenChild = dimenChild->rightSibling;
        }

        return DYNAMIC_INDEX;
    }
}

int isAllConstIndex(AST_NODE* headNode){
    if(!headNode)
        return 1;

    if(isAllConstIndex(headNode->rightSibling)){
        if(headNode->nodeType == CONST_VALUE_NODE)
            return 1;
    }
    return 0;
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
    /* border check */
    assert(regIndex >= 0);
    assert(regIndex <= pThis->numOfReg);

    pThis->regUser[regIndex] = nodeUseThisReg;
}

void releaseReg(RegisterManager* pThis, int regNum){
    /* release used register to free register.
     * break link from Register Manager to AST_NODE */
    int regIndex = regNum - pThis->firstRegNum;
    /* border check */
    assert(regIndex >= 0);
    assert(regIndex <= pThis->numOfReg);

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
    /* border check */
    assert(regIndex >= 0);
    assert(regIndex <= pThis->numOfReg);

    int regNum = regIndex + pThis->firstRegNum;
    if(pThis->regUser[regIndex]){
        /* if AST_NODE (register user) exist */
        ExpValPlace* place = &(pThis->regUser[regIndex]->valPlace);

        /* store value of register into stack */
        fprintf(targetFile, "sw $%d, %d($fp)\n", regNum, -1*(GR.stackTop + 4));
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
            releaseReg(GR.regManager, intRegNum);
        }
        else if(dataType == FLOAT_TYPE){
            int floatRegNum = getExprNodeReg(targetFile, ExprNode);
            fprintf(targetFile, "li $v0, 2\n");
            fprintf(targetFile, "mov.s $f12, $f%d\n", floatRegNum);
            fprintf(targetFile, "syscall\n");
            releaseReg(GR.FPRegManager, floatRegNum);
        }
    }

}
