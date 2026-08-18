package com.cdsfastrpctest

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BugReport
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Error
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Security
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.cdsfastrpctest.ui.theme.CDSPFastRPCTestTheme
import com.cdsfastrpctest.ui.theme.FailRed
import com.cdsfastrpctest.ui.theme.InfoBlue
import com.cdsfastrpctest.ui.theme.PassGreen
import com.cdsfastrpctest.ui.theme.QualcommCyan
import com.cdsfastrpctest.ui.theme.QualcommNavy
import com.cdsfastrpctest.ui.theme.TerminalBackground
import com.cdsfastrpctest.ui.theme.TerminalDim
import com.cdsfastrpctest.ui.theme.TerminalText
import com.cdsfastrpctest.ui.theme.WarningAmber
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

enum class TestState {
    WAITING,
    INITIALIZING,
    RUNNING,
    SUCCESS,
    FAILED
}

data class StageStatus(
    val name: String,
    val description: String,
    val state: StageState
)

enum class StageState {
    PENDING,
    ACTIVE,
    PASSED,
    FAILED
}

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            CDSPFastRPCTestTheme {
                MainDiagnosticScreen()
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainDiagnosticScreen() {
    val context = LocalContext.current
    val coroutineScope = rememberCoroutineScope()

    var testState by remember { mutableStateOf(TestState.WAITING) }
    var currentRunningStage by remember { mutableStateOf("WAITING") }
    var executionLogs by remember { mutableStateOf("Ready. Press [ INITIALIZE & RUN CDSP TEST ] to begin diagnostic execution.") }
    var failedStageName by remember { mutableStateOf<String?>(null) }
    var failedStageError by remember { mutableStateOf<String?>(null) }
    var dspResultValue by remember { mutableStateOf<Int?>(null) }

    val stages = listOf(
        "INITIALIZING" to "Environment & ABI Verification",
        "FAST RPC LIBRARY" to "Dynamic link libcdsprpc.so",
        "OPENING CDSP" to "Set FastRPC mode CDSP_DOMAIN_ID=3",
        "LOADING REMOTE OBJECT" to "remote_handle_open(calculator)",
        "HANDLE CREATED" to "Validate remote DSP handle descriptor",
        "REMOTE FUNCTION CALL" to "Execute test_add(1, 2) on CDSP",
        "RESULT" to "Validate return value (expected 3)",
        "CLOSE" to "Release FastRPC remote handle"
    )

    fun runDiagnosticTest() {
        if (testState == TestState.RUNNING) return
        testState = TestState.RUNNING
        currentRunningStage = "INITIALIZING"
        failedStageName = null
        failedStageError = null
        dspResultValue = null

        coroutineScope.launch {
            val sb = StringBuilder()
            sb.append("=== INITIATING CDSP FASTRPC HARDWARE LOADER PoC ===\n")
            sb.append("Target Device: Samsung Galaxy Tab A9+ 5G (SM-X216B)\n")
            sb.append("SoC: Qualcomm SM6375 / Snapdragon 695 (Blair)\n")
            sb.append("Android Version: ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})\n")
            sb.append("Architecture: ${Build.SUPPORTED_ABIS.joinToString()}\n\n")

            if (!FastRpcBridge.isLoaded()) {
                val err = FastRpcBridge.getLoadError() ?: "libcdsp_loader.so failed to load"
                sb.append("[FAIL] Native Library Loader: $err\n")
                sb.append("\nFINAL RESULT:\nREAL CDSP EXECUTION = FAIL\n")
                executionLogs = sb.toString()
                testState = TestState.FAILED
                currentRunningStage = "INITIALIZING"
                failedStageName = "INITIALIZING"
                failedStageError = err
                return@launch
            }

            // System environment diagnostic
            val sysDiag = withContext(Dispatchers.IO) {
                FastRpcBridge.checkSystemEnvironment()
            }
            sb.append(sysDiag).append("\n")

            // Execute actual FastRPC pipeline with test_add(1, 2)
            val nativeResult = withContext(Dispatchers.IO) {
                FastRpcBridge.executeCdspTest(1, 2)
            }
            sb.append(nativeResult)
            executionLogs = sb.toString()

            if (nativeResult.contains("REAL CDSP EXECUTION = PASS")) {
                testState = TestState.SUCCESS
                currentRunningStage = "COMPLETED"
                dspResultValue = 3
            } else {
                testState = TestState.FAILED
                val stageMatch = Regex("\\[FAIL\\] Stage: ([^\\n]+)").find(nativeResult)
                failedStageName = stageMatch?.groupValues?.get(1) ?: "FASTRPC EXECUTION"
                val errorMatch = Regex("Error:\\n([^\\n]+)").find(nativeResult)
                failedStageError = errorMatch?.groupValues?.get(1) ?: "Hardware or security boundary prevented CDSP execution."
                currentRunningStage = failedStageName ?: "FAILED"
            }
        }
    }

    fun probeDeviceNodes() {
        coroutineScope.launch {
            val sb = StringBuilder()
            sb.append("=== PROBING KERNEL DEVICE NODES & PERMISSIONS ===\n")
            sb.append("Process UID: ${android.os.Process.myUid()}\n\n")
            if (FastRpcBridge.isLoaded()) {
                val nodesInfo = withContext(Dispatchers.IO) {
                    FastRpcBridge.checkDeviceNodes()
                }
                sb.append(nodesInfo)
            } else {
                sb.append("Error: Native library not loaded.\n")
            }
            executionLogs = sb.toString()
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text(
                            text = "CDSP FastRPC Test",
                            style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onPrimaryContainer
                        )
                        Text(
                            text = "Qualcomm SM6375 / CDSP Loader PoC (SM-X216B)",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.8f)
                        )
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.primaryContainer
                ),
                actions = {
                    IconButton(
                        onClick = {
                            val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                            val clip = ClipData.newPlainText("CDSPFastRPCTest Log", executionLogs)
                            clipboard.setPrimaryClip(clip)
                            Toast.makeText(context, "Log copied to clipboard", Toast.LENGTH_SHORT).show()
                        },
                        modifier = Modifier.testTag("copy_log_button")
                    ) {
                        Icon(
                            imageVector = Icons.Default.ContentCopy,
                            contentDescription = "Copy Log",
                            tint = MaterialTheme.colorScheme.onPrimaryContainer
                        )
                    }
                }
            )
        }
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .background(MaterialTheme.colorScheme.background)
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // Target Hardware Profile Card
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
                elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Memory,
                            contentDescription = "Hardware Profile",
                            tint = QualcommNavy
                        )
                        Text(
                            text = "Target Hardware Specification",
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Bold
                        )
                    }
                    Spacer(modifier = Modifier.height(10.dp))
                    InfoRow("Device Target", "Samsung Galaxy Tab A9+ 5G (SM-X216B)")
                    InfoRow("SoC & Board", "Qualcomm SM6375 / Snapdragon 695 (Blair)")
                    InfoRow("Target DSP Domain", "CDSP (Compute DSP Domain = 3)")
                    InfoRow("OS & SELinux", "Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT}) / Enforcing")
                    InfoRow("Security Boundary", "Stock Retail Firmware / No Root / Locked BL")
                    InfoRow("Test Target", "test_add(1, 2) → 3 via Physical CDSP")
                }
            }

            // Real Hardware Result Verdict Banner
            when (testState) {
                TestState.SUCCESS -> {
                    VerdictBanner(
                        title = "REAL HARDWARE RESULT: CDSP EXECUTION = PASS",
                        subtitle = "Qualcomm CDSP executed test_add(1, 2) and returned 3 on physical SM-X216B.",
                        backgroundColor = PassGreen.copy(alpha = 0.15f),
                        borderColor = PassGreen,
                        icon = Icons.Default.CheckCircle,
                        iconTint = PassGreen,
                        testTag = "verdict_banner"
                    )
                }
                TestState.FAILED -> {
                    VerdictBanner(
                        title = "REAL HARDWARE RESULT: CDSP EXECUTION = FAIL",
                        subtitle = "Stage: ${failedStageName ?: "EXECUTION"}\nError: ${failedStageError ?: "FastRPC access blocked by Android security boundary."}",
                        backgroundColor = FailRed.copy(alpha = 0.15f),
                        borderColor = FailRed,
                        icon = Icons.Default.Error,
                        iconTint = FailRed,
                        testTag = "verdict_banner"
                    )
                }
                TestState.RUNNING -> {
                    VerdictBanner(
                        title = "DIAGNOSTIC TEST IN PROGRESS",
                        subtitle = "Executing FastRPC stage: $currentRunningStage...",
                        backgroundColor = InfoBlue.copy(alpha = 0.15f),
                        borderColor = InfoBlue,
                        icon = Icons.Default.Refresh,
                        iconTint = InfoBlue,
                        testTag = "verdict_banner",
                        isLoading = true
                    )
                }
                TestState.WAITING, TestState.INITIALIZING -> {
                    VerdictBanner(
                        title = "STATUS: WAITING",
                        subtitle = "Press [ INITIALIZE & RUN CDSP TEST ] to test direct FastRPC execution.",
                        backgroundColor = MaterialTheme.colorScheme.surfaceVariant,
                        borderColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.3f),
                        icon = Icons.Default.Security,
                        iconTint = QualcommNavy,
                        testTag = "verdict_banner"
                    )
                }
            }

            // Action Buttons
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Button(
                    onClick = { runDiagnosticTest() },
                    enabled = testState != TestState.RUNNING,
                    modifier = Modifier
                        .weight(1f)
                        .height(52.dp)
                        .testTag("run_test_button"),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = QualcommNavy,
                        contentColor = Color.White
                    )
                ) {
                    if (testState == TestState.RUNNING) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(20.dp),
                            color = Color.White,
                            strokeWidth = 2.dp
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("RUNNING...")
                    } else {
                        Icon(imageVector = Icons.Default.PlayArrow, contentDescription = null)
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(if (testState == TestState.WAITING) "INITIALIZE & RUN TEST" else "RE-RUN TEST", fontWeight = FontWeight.Bold)
                    }
                }

                OutlinedButton(
                    onClick = { probeDeviceNodes() },
                    enabled = testState != TestState.RUNNING,
                    modifier = Modifier
                        .height(52.dp)
                        .testTag("probe_nodes_button")
                ) {
                    Icon(imageVector = Icons.Default.BugReport, contentDescription = null)
                    Spacer(modifier = Modifier.width(4.dp))
                    Text("PROBE /DEV")
                }
            }

            // Pipeline Stages Visualizer Card
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
                elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text(
                        text = "FastRPC Pipeline Stages",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold
                    )
                    Spacer(modifier = Modifier.height(12.dp))

                    stages.forEachIndexed { index, (stageName, stageDesc) ->
                        val stageState = when {
                            testState == TestState.SUCCESS -> StageState.PASSED
                            testState == TestState.FAILED && failedStageName == stageName -> StageState.FAILED
                            testState == TestState.FAILED && stages.indexOfFirst { it.first == failedStageName } < index -> StageState.PENDING
                            testState == TestState.FAILED -> StageState.PASSED
                            testState == TestState.RUNNING && currentRunningStage == stageName -> StageState.ACTIVE
                            testState == TestState.RUNNING -> StageState.PENDING
                            else -> StageState.PENDING
                        }

                        StageRowItem(
                            stepNumber = index + 1,
                            name = stageName,
                            description = stageDesc,
                            state = stageState
                        )

                        if (index < stages.size - 1) {
                            Box(
                                modifier = Modifier
                                    .padding(start = 15.dp)
                                    .width(2.dp)
                                    .height(10.dp)
                                    .background(
                                        if (stageState == StageState.PASSED) PassGreen else MaterialTheme.colorScheme.outline.copy(alpha = 0.2f)
                                    )
                            )
                        }
                    }
                }
            }

            // Diagnostic Console / Execution Log Card
            Card(
                modifier = Modifier
                    .fillMaxWidth()
                    .testTag("log_console"),
                colors = CardDefaults.cardColors(containerColor = TerminalBackground),
                shape = RoundedCornerShape(8.dp)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "DIAGNOSTIC LOG & HARDWARE TRUTH REPORT",
                            style = MaterialTheme.typography.labelSmall,
                            color = QualcommCyan,
                            fontWeight = FontWeight.Bold
                        )
                        Text(
                            text = "STRICT TRUTH POLICY",
                            style = MaterialTheme.typography.labelSmall,
                            color = TerminalDim
                        )
                    }
                    Spacer(modifier = Modifier.height(8.dp))
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(Color.Black.copy(alpha = 0.4f), RoundedCornerShape(4.dp))
                            .padding(12.dp)
                    ) {
                        Text(
                            text = executionLogs,
                            style = MaterialTheme.typography.bodySmall.copy(
                                fontFamily = FontFamily.Monospace,
                                fontSize = 11.5.sp,
                                lineHeight = 17.sp
                            ),
                            color = TerminalText
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun InfoRow(label: String, value: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.SemiBold),
            color = MaterialTheme.colorScheme.onSurface
        )
    }
}

@Composable
fun VerdictBanner(
    title: String,
    subtitle: String,
    backgroundColor: Color,
    borderColor: Color,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    iconTint: Color,
    testTag: String,
    isLoading: Boolean = false
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .border(1.5.dp, borderColor, RoundedCornerShape(12.dp))
            .testTag(testTag),
        colors = CardDefaults.cardColors(containerColor = backgroundColor),
        shape = RoundedCornerShape(12.dp)
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            if (isLoading) {
                CircularProgressIndicator(
                    modifier = Modifier.size(28.dp),
                    color = iconTint,
                    strokeWidth = 3.dp
                )
            } else {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = iconTint,
                    modifier = Modifier.size(32.dp)
                )
            }
            Column {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface
                )
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
fun StageRowItem(
    stepNumber: Int,
    name: String,
    description: String,
    state: StageState
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        val (badgeBg, badgeText, icon) = when (state) {
            StageState.PASSED -> Triple(PassGreen, Color.White, "✓")
            StageState.FAILED -> Triple(FailRed, Color.White, "✗")
            StageState.ACTIVE -> Triple(QualcommNavy, Color.White, "…")
            StageState.PENDING -> Triple(MaterialTheme.colorScheme.surfaceVariant, MaterialTheme.colorScheme.onSurfaceVariant, "$stepNumber")
        }

        Box(
            modifier = Modifier
                .size(30.dp)
                .clip(CircleShape)
                .background(badgeBg),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = icon,
                style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
                color = badgeText
            )
        }

        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = name,
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Bold),
                color = when (state) {
                    StageState.PASSED -> PassGreen
                    StageState.FAILED -> FailRed
                    StageState.ACTIVE -> QualcommNavy
                    StageState.PENDING -> MaterialTheme.colorScheme.onSurface
                }
            )
            Text(
                text = description,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }

        Text(
            text = when (state) {
                StageState.PASSED -> "PASS"
                StageState.FAILED -> "FAIL"
                StageState.ACTIVE -> "RUNNING"
                StageState.PENDING -> "WAITING"
            },
            style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
            color = when (state) {
                StageState.PASSED -> PassGreen
                StageState.FAILED -> FailRed
                StageState.ACTIVE -> QualcommNavy
                StageState.PENDING -> TerminalDim
            }
        )
    }
}
