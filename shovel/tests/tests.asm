Function f0
-locals 1
-closures 1
PushString "Test"
Require
StoreClosure 0
PushFunction f1
Store 0
Load 0
CallFunctionNoReturn 0
Function f1
-locals 1
-closures 1
PushFunction f2
StoreClosure 0
PushString "Recursive Fibonacci"
PushString "begin"
LoadClosure 1
LoadElement
CallFunctionNoReturn 1
PushUnsignedInteger 5
LoadClosure 0
CallFunction 1
Store 0
Load 0
PushString "end"
LoadClosure 1
LoadElement
CallFunctionNoReturn 1
Function f2
-parameters 1
Load 0
PushUnsignedInteger 1
LessThanOrEquals
JumpIfFalse l1
Load 0
Return
.l1
Load 0
PushUnsignedInteger 1
Subtract
LoadClosure 0
CallFunction 1
Load 0
PushUnsignedInteger 2
Subtract
LoadClosure 0
CallFunction 1
Add
Return
