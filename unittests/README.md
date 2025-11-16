# Local Variable Scoping - Test Suite and Findings

## Problem Summary
The Kuyil compiler has a design flaw where local variables are "implicitly on the stack" without proper SET_LOCAL/GET_LOCAL bytecode. This works accidentally when:
- Stack position == Slot number
- No nested scopes cause slot reuse
  
But fails when:
- Nested scopes cause slot reuse (compiler's `end_scope()` frees slots)
- Stack position doesn't match slot number

## Current Workaround (Implemented)
**Avatar Runtime Stack Corruption Fix:**
1. Added `max_local_count` to Compiler struct to track peak locals
2. Added `local_count` to Function struct for runtime metadata
3. Avatar runtime blocks OP_POP operations that would shrink stack below local_count
4. Uses `locals_initialized` flag to avoid blocking during initialization phase

**Status:** Works for simple cases like `test_double_access.kyl` where stack alignment is lucky.

## Root Cause
**Compiler Bug in `compile_var_decl()` (src/compiler.c:849-859):**
```c
if (current->scope_depth > 0) {
    // Local variable
    add_local(node->as.var_decl.name);
    if (node->as.var_decl.value) {
        compile_expression(node->as.var_decl.value);
    } else {
        emit_byte(OP_NIL);
    }
    // Local variables are implicitly on the stack  ← WRONG!
}
```

**What's Missing:**
- No `SET_LOCAL` to copy value from expression stack to local slot
- No `POP` to remove value from expression stack
- Comment "implicitly on the stack" is misleading

## Proper Solution (Not Yet Implemented)
The compiler should emit:
```c
compile_expression(value);  // Push value onto stack
emit_bytes(OP_SET_LOCAL, slot);  // Copy to local slot
emit_byte(OP_POP);  // Remove from expression stack
```

But this requires understanding:
1. **Stack Model:** Are locals part of stack or separate slots?
2. **VM Architecture:** How does GET_LOCAL work? From stack or slots?
3. **Scope Management:** How does end_scope() interact with stack_top?

## Test Cases Created

### Test 1: Basic Local (`test_locals_basic.kyl`)
- Single local variable in function
- **Expected:** Works with current workaround
- **Purpose:** Baseline test

### Test 2: Multiple Locals (`test_locals_multiple.kyl`)
- Three sequential local variables
- **Expected:** May work if slots align with stack
- **Purpose:** Test sequential allocation

### Test 3: After Nested Scope (`test_locals_after_scope.kyl`)
- Local declared after if-block
- **Expected:** FAILS - slot reuse causes misalignment
- **Purpose:** Reproduce IDE bug

### Test 4: Function Call Result (`test_locals_function_call.kyl`)
- Local assigned from function call
- **Expected:** Works if return value pushed correctly
- **Purpose:** Test call stack interaction

### Test 5: Object from Call (`test_locals_object_call.kyl`)
- Local object from function, access properties
- **Expected:** Works if object stays on stack
- **Purpose:** Test object references

### Test 6: Complex (`test_locals_complex.kyl`)
- Multiple scopes, function calls, objects
- **Expected:** FAILS - reproduces IDE scenario
- **Purpose:** Comprehensive real-world test

## Next Steps
1. **Understand VM Architecture:**
   - Read opcode_executor.c GET_LOCAL implementation
   - Understand frame->slots vs stack relationship
   - Document actual vs intended behavior

2. **Fix Compiler:**
   - Emit proper SET_LOCAL + POP for local var decls
   - Ensure stack_top correctness
   - Update end_scope() if needed

3. **Validate:**
   - Run all unit tests
   - Test IDE scenario
   - Verify no regressions

## Current Status
- ✅ Stack corruption workaround works for simple aligned cases
- ❌ Complex cases with nested scopes still fail
- ⚠️ Need proper compiler fix for long-term solution
- 📝 Test suite created to validate fixes
