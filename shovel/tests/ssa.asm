Function f0
-closures 3
PushNull
PushUnsignedInteger 0
StoreClosure 0
PushFunction f1
StoreClosure 1
PushFunction f2
StoreClosure 2
PushFunction f3
Store 0
PushUnsignedInteger 100
Grab 1
CallFunctionNoReturn 1
Function f1
-parameters 1
LoadClosure 0
Grab 1
Add
StoreClosure 0
Function f2
-parameters 1
Grab 0
Return
Function f3
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
LoadClosure 2
CallPureFunction 1
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
Grab 6
Grab 5
Add
LoadClosure 2
CallPureFunction 1
Add
Store 2
Grab 2
LoadClosure 1
CallFunctionNoReturn 1
Grab 1
Grab 5
LoadClosure 2
CallPureFunction 1
Add
Store 1
.l1
Grab 0
Increment
Store 0
Jump l0
.l2
Grab 2
Return
