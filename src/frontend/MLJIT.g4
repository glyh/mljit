grammar MLJIT;

// ════════════════════════════════════════════════════════════════
//  Lexer rules
// ════════════════════════════════════════════════════════════════

INT     : [0-9]+ ;
ID      : [a-zA-Z_][a-zA-Z0-9_]* ;
WS      : [ \t\r\n]+ -> skip ;
LINE_CMT: '(*' .*? '*)' -> skip ;

// ════════════════════════════════════════════════════════════════
//  Parser rules
// ════════════════════════════════════════════════════════════════

program
    : funDef+ EOF
    ;

funDef
    : 'fun' ID '(' (ID (',' ID)*)? ')' '=' expr
    ;

// Expression operators in ascending precedence
expr
    : ifExpr    # IfExp
    | letExpr   # LetExp
    | eqExpr    # EqAlt
    ;

eqExpr
    : cmpExpr                       # CmpAlt
    | eqExpr op=('==' | '!=') cmpExpr  # EqBin
    ;

cmpExpr
    : addExpr                       # AddAlt
    | cmpExpr op=('<' | '<=' | '>' | '>=') addExpr  # CmpBin
    ;

addExpr
    : mulExpr                  # MulAlt
    | addExpr op=('+' | '-') mulExpr  # AddBin
    ;

mulExpr
    : unaryExpr                # UnaryAlt
    | mulExpr op=('*' | '/' | '%') unaryExpr  # MulBin
    ;

unaryExpr
    : '-' unaryExpr # Negate
    | primaryExpr   # Primary
    ;

primaryExpr
    : INT                                # IntLit
    | ID '(' (expr (',' expr)*)? ')'    # Call
    | ID                                 # Variable
    | '(' expr ')'                       # Parens
    ;

ifExpr
    : 'if' expr 'then' expr 'else' expr
    ;

letExpr
    : 'let' binding (';' binding)* 'in' expr 'end'
    ;

binding
    : ID '=' expr
    ;
