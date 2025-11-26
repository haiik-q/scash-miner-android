package com.scash.miner;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Spinner;
import android.widget.TextView;
import androidx.appcompat.app.AppCompatActivity;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public class MainActivity extends AppCompatActivity {

    static {
        System.loadLibrary("scashminer");
    }

    private EditText walletAddressInput;
    private TextView addressValidation;
    private Button startMiningButton;
    private TextView statusText;
    private TextView hashrateText;
    private TextView logText;
    private TextView cpuCoresText;
    private Spinner threadSpinner;

    private boolean isMining = false;
    private Handler uiHandler;
    private Thread miningThread;
    private int cpuCores;
    private int selectedThreads;

    private static final String DEFAULT_POOL = "pool.scash.pro:7777";
    private static final String DEV_WALLET = "scash1qx02tpyuhy6w55caa383jmxmqukp8jnsnz6g02s";
    private static final float DEV_FEE = 0.05f; // 5%

    // Native methods
    private native int startMining(String poolUrl, String walletAddress, String devWallet, float devFee, int threads);
    private native void stopMining();
    private native double getHashrate();
    private native String getMiningStatus();
    private native boolean isPoolConnected();
    private native int getCpuCores();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        uiHandler = new Handler(Looper.getMainLooper());

        walletAddressInput = findViewById(R.id.walletAddressInput);
        addressValidation = findViewById(R.id.addressValidation);
        startMiningButton = findViewById(R.id.startMiningButton);
        statusText = findViewById(R.id.statusText);
        hashrateText = findViewById(R.id.hashrateText);
        logText = findViewById(R.id.logText);
        cpuCoresText = findViewById(R.id.cpuCoresText);
        threadSpinner = findViewById(R.id.threadSpinner);

        // Initialize CPU cores and thread selector
        initializeCpuCores();
        initializeThreadSpinner();

        // Wallet address validation
        walletAddressInput.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
                validateWalletAddress(s.toString());
            }

            @Override
            public void afterTextChanged(Editable s) {}
        });

        // Start/Stop mining button
        startMiningButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (!isMining) {
                    startMiningProcess();
                } else {
                    stopMiningProcess();
                }
            }
        });
    }

    private boolean validateWalletAddress(String address) {
        if (address.isEmpty()) {
            addressValidation.setVisibility(View.GONE);
            return false;
        }

        // Scash address validation: starts with "scash1" and is 42 characters long
        if (!address.startsWith("scash1")) {
            addressValidation.setText("地址必须以 scash1 开头 (Address must start with scash1)");
            addressValidation.setTextColor(0xFFFF0000);
            addressValidation.setVisibility(View.VISIBLE);
            return false;
        }

        if (address.length() != 42) {
            addressValidation.setText("地址长度必须是42个字符 (Address must be 42 characters)");
            addressValidation.setTextColor(0xFFFF0000);
            addressValidation.setVisibility(View.VISIBLE);
            return false;
        }

        // Check if address contains only valid bech32 characters (lowercase letters and numbers)
        String validChars = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
        String addressBody = address.substring(6); // Skip "scash1"
        for (char c : addressBody.toCharArray()) {
            if (validChars.indexOf(c) == -1) {
                addressValidation.setText("地址包含无效字符 (Address contains invalid characters)");
                addressValidation.setTextColor(0xFFFF0000);
                addressValidation.setVisibility(View.VISIBLE);
                return false;
            }
        }

        addressValidation.setText("✓ 地址格式正确 (Address format is valid)");
        addressValidation.setTextColor(0xFF00FF00);
        addressValidation.setVisibility(View.VISIBLE);
        return true;
    }

    private void startMiningProcess() {
        String walletAddress = walletAddressInput.getText().toString().trim();

        if (!validateWalletAddress(walletAddress)) {
            addLog("错误: 请输入有效的钱包地址 (Error: Please enter a valid wallet address)");
            return;
        }

        isMining = true;
        startMiningButton.setText("停止挖矿 (Stop Mining)");
        startMiningButton.setBackgroundTintList(getResources().getColorStateList(android.R.color.holo_red_dark));
        statusText.setText("正在连接矿池... (Connecting to pool...)");
        statusText.setTextColor(0xFFFFFF00);

        addLog("开始挖矿 (Starting mining)...");
        addLog("矿池 (Pool): " + DEFAULT_POOL);
        addLog("钱包地址 (Wallet): " + walletAddress);
        addLog("线程数 (Threads): " + selectedThreads);
        addLog("开发者费用 (Dev fee): " + (DEV_FEE * 100) + "%");

        // Start mining in a separate thread
        miningThread = new Thread(new Runnable() {
            @Override
            public void run() {
                int result = startMining(DEFAULT_POOL, walletAddress, DEV_WALLET, DEV_FEE, selectedThreads);

                if (result != 0) {
                    uiHandler.post(new Runnable() {
                        @Override
                        public void run() {
                            addLog("错误: 启动挖矿失败 (Error: Failed to start mining)");
                            stopMiningProcess();
                        }
                    });
                    return;
                }

                // Start monitoring thread
                startMonitoring();
            }
        });
        miningThread.start();
    }

    private void stopMiningProcess() {
        isMining = false;
        stopMining();

        if (miningThread != null) {
            miningThread.interrupt();
        }

        startMiningButton.setText("开始挖矿 (Start Mining)");
        startMiningButton.setBackgroundTintList(getResources().getColorStateList(android.R.color.holo_green_dark));
        statusText.setText("已停止 (Stopped)");
        statusText.setTextColor(0xFFFF0000);
        hashrateText.setText("0.00 H/s");

        addLog("已停止挖矿 (Mining stopped)");
    }

    private void startMonitoring() {
        final long startTime = System.currentTimeMillis();
        final long CONNECTION_TIMEOUT = 3 * 60 * 1000; // 3 minutes

        new Thread(new Runnable() {
            @Override
            public void run() {
                while (isMining) {
                    try {
                        Thread.sleep(1000); // Update every second

                        final double hashrate = getHashrate();
                        final String status = getMiningStatus();
                        final boolean poolConnected = isPoolConnected();

                        // Check connection timeout
                        long elapsedTime = System.currentTimeMillis() - startTime;
                        if (!poolConnected && elapsedTime > CONNECTION_TIMEOUT) {
                            uiHandler.post(new Runnable() {
                                @Override
                                public void run() {
                                    addLog("错误: 连接矿池超时 (Error: Pool connection timeout)");
                                    stopMiningProcess();
                                }
                            });
                            break;
                        }

                        uiHandler.post(new Runnable() {
                            @Override
                            public void run() {
                                // Update hashrate
                                if (hashrate >= 1000000) {
                                    hashrateText.setText(String.format(Locale.US, "%.2f MH/s", hashrate / 1000000));
                                } else if (hashrate >= 1000) {
                                    hashrateText.setText(String.format(Locale.US, "%.2f kH/s", hashrate / 1000));
                                } else {
                                    hashrateText.setText(String.format(Locale.US, "%.2f H/s", hashrate));
                                }

                                // Update status
                                if (poolConnected) {
                                    statusText.setText("挖矿中 (Mining): " + status);
                                    statusText.setTextColor(0xFF00FF00);
                                } else {
                                    statusText.setText("连接中 (Connecting): " + status);
                                    statusText.setTextColor(0xFFFFFF00);
                                }
                            }
                        });

                    } catch (InterruptedException e) {
                        break;
                    }
                }
            }
        }).start();
    }

    private void initializeCpuCores() {
        // Get CPU cores from native code
        cpuCores = getCpuCores();
        if (cpuCores <= 0) {
            cpuCores = Runtime.getRuntime().availableProcessors();
        }
        cpuCoresText.setText("CPU: " + cpuCores + "核");
        addLog("检测到CPU核心数 (Detected CPU cores): " + cpuCores);
    }

    private void initializeThreadSpinner() {
        // Create thread options (1 to cpuCores)
        List<String> threadOptions = new ArrayList<>();
        for (int i = 1; i <= cpuCores; i++) {
            threadOptions.add(String.valueOf(i));
        }

        // Create adapter
        ArrayAdapter<String> adapter = new ArrayAdapter<>(
                this,
                R.layout.spinner_item,
                threadOptions
        );
        adapter.setDropDownViewResource(R.layout.spinner_dropdown_item);
        threadSpinner.setAdapter(adapter);

        // Set default selection to max cores
        threadSpinner.setSelection(cpuCores - 1);
        selectedThreads = cpuCores;

        // Listen for selection changes
        threadSpinner.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(android.widget.AdapterView<?> parent, View view, int position, long id) {
                selectedThreads = position + 1;
                addLog("选择挖矿线程数 (Selected threads): " + selectedThreads);
            }

            @Override
            public void onNothingSelected(android.widget.AdapterView<?> parent) {
                selectedThreads = cpuCores;
            }
        });
    }

    private void addLog(final String message) {
        uiHandler.post(new Runnable() {
            @Override
            public void run() {
                String currentLog = logText.getText().toString();
                String timestamp = new java.text.SimpleDateFormat("HH:mm:ss", Locale.US).format(new java.util.Date());
                logText.setText(currentLog + "[" + timestamp + "] " + message + "\n");

                // Auto-scroll to bottom
                final ScrollView scrollView = (ScrollView) logText.getParent();
                scrollView.post(new Runnable() {
                    @Override
                    public void run() {
                        scrollView.fullScroll(View.FOCUS_DOWN);
                    }
                });
            }
        });
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (isMining) {
            stopMiningProcess();
        }
    }
}
