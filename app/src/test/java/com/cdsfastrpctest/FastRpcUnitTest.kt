package com.cdsfastrpctest

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Test

class FastRpcUnitTest {

    @Test
    fun testExpectedArithmetic() {
        val a = 1
        val b = 2
        val expected = 3
        assertEquals(expected, a + b)
    }

    @Test
    fun testBridgeClassExists() {
        val bridge = FastRpcBridge
        assertNotNull(bridge)
    }
}
