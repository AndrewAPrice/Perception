Function f0
-closures 2
PushNull
PushFunction f1
StoreClosure 0
LoadClosure 0
StoreClosure 1
PushFunction f2
Store 0
PushUnsignedInteger 100
Grab 1
CallFunctionNoReturn 1
Function f1
-parameters 1
Grab 0
Return
Function f2
-parameters 1
PushManyNulls 5
PushUnsignedInteger 2
Store 4
PushUnsignedInteger 3
PushUnsignedInteger 2
Subtract
Store 3
Grab 5
Grab 4
Add
LoadClosure 1
CallFunction 1
Grab 5
Multiply
Store 2
PushUnsignedInteger 0
Store 1
PushUnsignedInteger 0
Store 0
.l0
Grab 0
PushUnsignedInteger 10
LessThan
JumpIfFalse l2
Grab 2
Grab 5
Grab 4
Add
LoadClosure 1
CallPureFunction 1
Add
Store 1
Grab 2
LoadClosure 0
CallFunctionNoReturn 1
Grab 1
Grab 4
LoadClosure 1
CallPureFunction 1
Add
Store 0
.l1
Grab 0
Increment
Store 0
Jump l0
.l2
Grab 2
Return
