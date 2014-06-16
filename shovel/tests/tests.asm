Function f0
-closures 1
PushNull
PushString "Test"
Require
StoreClosure 0
PushFunction f1
Store 0
Grab 0
CallFunctionNoReturn 0
Function f1
-closures 1
PushNull
PushFunction f2
StoreClosure 0
PushString "Recursive Fibonacci"
PushString "begin"
LoadClosure 1
LoadElement
CallFunctionNoReturn 1
PushUnsignedInteger 35
LoadClosure 0
CallFunction 1
Store 0
Grab 0
PushString "end"
LoadClosure 1
LoadElement
CallFunctionNoReturn 1
Function f2
-parameters 1
Grab 0
PushUnsignedInteger 1
LessThanOrEquals
JumpIfFalse l1
Return
.l1
Grab 0
PushUnsignedInteger 1
Subtract
LoadClosure 0
CallFunction 1
Grab 1
PushUnsignedInteger 2
Subtract
LoadClosure 0
CallFunction 1
Add
Store 0
Return
