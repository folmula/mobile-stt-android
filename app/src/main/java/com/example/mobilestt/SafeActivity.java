package com.example.mobilestt;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class SafeActivity extends Activity {

    private static final int REQ_PICK_AUDIO = 1001;
    private static final int REQ_PICK_MODEL = 1002;
    private static final int REQ_CREATE_TXT = 1003;

    private TextView statusText;
    private TextView audioText;
    private TextView modelText;
    private TextView copyText;

    private Button pickAudioButton;
    private Button pickModelButton;
    private Button copyButton;
    private Button nativeCheckButton;
    private Button modelLoadButton;
    private Button saveTxtButton;

    private Uri selectedAudioUri;
    private Uri selectedModelUri;

    private String selectedAudioName = "";
    private String selectedModelName = "";

    private long selectedAudioSize = -1L;
    private long selectedModelSize = -1L;

    private File copiedAudioFile;
    private File copiedModelFile;

    private boolean nativeLoadTried = false;
    private boolean nativeLibraryLoaded = false;
    private String nativeLoadError = "";

    private String lastNativeCheckResult = "아직 실행하지 않음";
    private String lastModelLoadResult = "아직 실행하지 않음";

    private native String nativeCheck();

    private native String nativeLoadModel(String modelPath);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        try {
            buildUi();
        } catch (Throwable e) {
            showFatalError(e);
        }
    }

    private void buildUi() {
        ScrollView scrollView = new ScrollView(this);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(24), dp(24), dp(24));

        TextView title = new TextView(this);
        title.setText("Mobile STT");
        title.setTextSize(26f);
        title.setTypeface(Typeface.DEFAULT_BOLD);

        statusText = new TextView(this);
        statusText.setText(
                "5단계 테스트입니다.\n\n" +
                "1. WAV 오디오 파일을 선택합니다.\n" +
                "2. Whisper 모델 파일을 선택합니다.\n" +
                "3. 두 파일을 앱 내부 저장소로 복사합니다.\n" +
                "4. whisper.cpp 네이티브 연결을 확인합니다.\n" +
                "5. 복사된 Whisper 모델을 실제로 로드합니다.\n\n" +
                "아직 STT 변환은 하지 않습니다."
        );
        statusText.setTextSize(16f);
        statusText.setPadding(0, dp(20), 0, dp(16));

        audioText = new TextView(this);
        audioText.setText("선택된 오디오 파일: 없음");
        audioText.setTextSize(15f);
        audioText.setPadding(0, dp(8), 0, dp(12));

        modelText = new TextView(this);
        modelText.setText("선택된 Whisper 모델 파일: 없음");
        modelText.setTextSize(15f);
        modelText.setPadding(0, dp(8), 0, dp(12));

        copyText = new TextView(this);
        copyText.setText("복사된 파일: 없음");
        copyText.setTextSize(15f);
        copyText.setPadding(0, dp(8), 0, dp(16));

        pickAudioButton = new Button(this);
        pickAudioButton.setText("1. WAV 오디오 파일 선택");
        pickAudioButton.setAllCaps(false);
        pickAudioButton.setOnClickListener(v -> {
            try {
                openAudioPicker();
            } catch (Throwable e) {
                showError("오디오 선택 화면 열기 실패", e);
            }
        });

        pickModelButton = new Button(this);
        pickModelButton.setText("2. Whisper 모델 파일 선택");
        pickModelButton.setAllCaps(false);
        pickModelButton.setOnClickListener(v -> {
            try {
                openModelPicker();
            } catch (Throwable e) {
                showError("모델 선택 화면 열기 실패", e);
            }
        });

        copyButton = new Button(this);
        copyButton.setText("3. 선택한 파일 앱 내부로 복사");
        copyButton.setAllCaps(false);
        copyButton.setEnabled(false);
        copyButton.setOnClickListener(v -> copySelectedFiles());

        nativeCheckButton = new Button(this);
        nativeCheckButton.setText("4. whisper.cpp 네이티브 연결 확인");
        nativeCheckButton.setAllCaps(false);
        nativeCheckButton.setOnClickListener(v -> runNativeCheck());

        modelLoadButton = new Button(this);
        modelLoadButton.setText("5. Whisper 모델 로드 확인");
        modelLoadButton.setAllCaps(false);
        modelLoadButton.setEnabled(false);
        modelLoadButton.setOnClickListener(v -> runModelLoadTest());

        saveTxtButton = new Button(this);
        saveTxtButton.setText("6. 결과 TXT 저장");
        saveTxtButton.setAllCaps(false);
        saveTxtButton.setEnabled(false);
        saveTxtButton.setOnClickListener(v -> {
            try {
                openTxtCreator();
            } catch (Throwable e) {
                showError("TXT 저장 화면 열기 실패", e);
            }
        });

        addView(root, title);
        addView(root, statusText);
        addView(root, audioText);
        addView(root, modelText);
        addView(root, copyText);
        addView(root, pickAudioButton);
        addView(root, pickModelButton);
        addView(root, copyButton);
        addView(root, nativeCheckButton);
        addView(root, modelLoadButton);
        addView(root, saveTxtButton);

        scrollView.addView(root);
        setContentView(scrollView);
    }

    private void addView(LinearLayout root, android.view.View view) {
        root.addView(
                view,
                new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                )
        );
    }

    private void openAudioPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");

        String[] mimeTypes = new String[]{
                "audio/wav",
                "audio/x-wav",
                "audio/wave",
                "audio/*",
                "application/octet-stream"
        };

        intent.putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);

        startActivityForResult(intent, REQ_PICK_AUDIO);
    }

    private void openModelPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");

        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);

        startActivityForResult(intent, REQ_PICK_MODEL);
    }

    private void openTxtCreator() {
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("text/plain");

        String fileName = "mobile_stt_model_load_test_" + nowForFileName() + ".txt";
        intent.putExtra(Intent.EXTRA_TITLE, fileName);

        startActivityForResult(intent, REQ_CREATE_TXT);
    }

    @Override
    protected void onActivityResult(
            int requestCode,
            int resultCode,
            Intent data
    ) {
        super.onActivityResult(requestCode, resultCode, data);

        try {
            if (resultCode != RESULT_OK || data == null || data.getData() == null) {
                return;
            }

            Uri uri = data.getData();

            if (requestCode == REQ_PICK_AUDIO) {
                handlePickedAudio(uri, data);
            } else if (requestCode == REQ_PICK_MODEL) {
                handlePickedModel(uri, data);
            } else if (requestCode == REQ_CREATE_TXT) {
                writeTestTxt(uri);
            }
        } catch (Throwable e) {
            showError("선택/저장 처리 중 오류", e);
        }
    }

    private void handlePickedAudio(Uri uri, Intent data) {
        selectedAudioUri = uri;
        selectedAudioName = getDisplayName(uri);
        selectedAudioSize = getFileSize(uri);

        persistReadPermission(uri, data);

        audioText.setText(
                "선택된 오디오 파일:\n" +
                selectedAudioName + "\n" +
                "크기: " + formatBytes(selectedAudioSize) + "\n\n" +
                "URI:\n" +
                selectedAudioUri
        );

        copiedAudioFile = null;
        copiedModelFile = null;
        copyText.setText("복사된 파일: 없음");
        lastModelLoadResult = "아직 실행하지 않음";

        statusText.setText(
                "오디오 파일 선택 완료.\n\n" +
                "이제 Whisper 모델 파일을 선택하세요."
        );

        updateButtons();
    }

    private void handlePickedModel(Uri uri, Intent data) {
        selectedModelUri = uri;
        selectedModelName = getDisplayName(uri);
        selectedModelSize = getFileSize(uri);

        persistReadPermission(uri, data);

        modelText.setText(
                "선택된 Whisper 모델 파일:\n" +
                selectedModelName + "\n" +
                "크기: " + formatBytes(selectedModelSize) + "\n\n" +
                "URI:\n" +
                selectedModelUri
        );

        copiedAudioFile = null;
        copiedModelFile = null;
        copyText.setText("복사된 파일: 없음");
        lastModelLoadResult = "아직 실행하지 않음";

        statusText.setText(
                "Whisper 모델 파일 선택 완료.\n\n" +
                "이제 '선택한 파일 앱 내부로 복사' 버튼을 누르세요."
        );

        updateButtons();
    }

    private void persistReadPermission(Uri uri, Intent data) {
        try {
            int flags = data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION;
            if (flags != 0) {
                getContentResolver().takePersistableUriPermission(uri, flags);
            }
        } catch (Throwable ignored) {
        }
    }

    private void copySelectedFiles() {
        if (selectedAudioUri == null || selectedModelUri == null) {
            Toast.makeText(this, "오디오와 모델 파일을 먼저 선택하세요.", Toast.LENGTH_LONG).show();
            return;
        }

        setButtonsEnabled(false);

        copiedAudioFile = null;
        copiedModelFile = null;
        lastModelLoadResult = "아직 실행하지 않음";

        final Uri audioUri = selectedAudioUri;
        final Uri modelUri = selectedModelUri;
        final String audioName = selectedAudioName;
        final String modelName = selectedModelName;
        final long audioSize = selectedAudioSize;
        final long modelSize = selectedModelSize;

        statusText.setText("파일 복사를 시작합니다...\n\n모델 파일은 크면 시간이 걸릴 수 있습니다.");

        new Thread(() -> {
            try {
                File audioOut = new File(
                        getCacheDir(),
                        "input_" + nowForFileName() + "_" + sanitizeFileName(audioName, "input.wav")
                );

                copyUriToFile(
                        audioUri,
                        audioOut,
                        "오디오 파일",
                        audioSize
                );

                File modelDir = getModelDirectory();
                if (!modelDir.exists() && !modelDir.mkdirs()) {
                    throw new IOException("모델 저장 폴더 생성 실패: " + modelDir.getAbsolutePath());
                }

                File modelOut = new File(
                        modelDir,
                        sanitizeFileName(modelName, "whisper_model.bin")
                );

                copyUriToFile(
                        modelUri,
                        modelOut,
                        "Whisper 모델 파일",
                        modelSize
                );

                copiedAudioFile = audioOut;
                copiedModelFile = modelOut;

                runOnUiThread(() -> {
                    copyText.setText(
                            "복사된 오디오 파일:\n" +
                            copiedAudioFile.getAbsolutePath() + "\n" +
                            "크기: " + formatBytes(copiedAudioFile.length()) + "\n\n" +
                            "복사된 모델 파일:\n" +
                            copiedModelFile.getAbsolutePath() + "\n" +
                            "크기: " + formatBytes(copiedModelFile.length())
                    );

                    statusText.setText(
                            "파일 복사 성공!\n\n" +
                            "이제 'Whisper 모델 로드 확인' 버튼을 눌러보세요.\n\n" +
                            "처음에는 tiny 또는 base 모델로 테스트하는 것을 권장합니다."
                    );

                    Toast.makeText(this, "파일 복사 완료", Toast.LENGTH_LONG).show();
                    updateButtons();
                });

            } catch (Throwable e) {
                runOnUiThread(() -> {
                    showError("파일 복사 실패", e);
                    updateButtons();
                });
            }
        }).start();
    }

    private void runNativeCheck() {
        try {
            if (!ensureNativeLibraryLoaded()) {
                lastNativeCheckResult =
                        "네이티브 라이브러리 로드 실패\n\n" +
                        nativeLoadError;

                statusText.setText(lastNativeCheckResult);
                Toast.makeText(this, "네이티브 로드 실패", Toast.LENGTH_LONG).show();
                return;
            }

            String result = nativeCheck();

            if (result == null || result.trim().isEmpty()) {
                result = "nativeCheck returned empty result";
            }

            lastNativeCheckResult = result;

            statusText.setText(
                    "네이티브 연결 확인 성공!\n\n" +
                    result
            );

            Toast.makeText(this, "whisper.cpp 연결 확인 성공", Toast.LENGTH_LONG).show();

        } catch (Throwable e) {
            lastNativeCheckResult =
                    e.getClass().getSimpleName() + "\n" +
                    String.valueOf(e.getMessage());

            showError("네이티브 연결 확인 실패", e);
        }
    }

    private void runModelLoadTest() {
        if (copiedModelFile == null || !copiedModelFile.exists()) {
            Toast.makeText(this, "먼저 모델 파일을 앱 내부로 복사하세요.", Toast.LENGTH_LONG).show();
            return;
        }

        if (!ensureNativeLibraryLoaded()) {
            lastModelLoadResult =
                    "네이티브 라이브러리 로드 실패\n\n" +
                    nativeLoadError;

            statusText.setText(lastModelLoadResult);
            Toast.makeText(this, "네이티브 로드 실패", Toast.LENGTH_LONG).show();
            return;
        }

        final File modelFile = copiedModelFile;

        setButtonsEnabled(false);

        statusText.setText(
                "Whisper 모델 로드 중...\n\n" +
                "모델 경로:\n" +
                modelFile.getAbsolutePath() + "\n\n" +
                "모델이 크면 시간이 오래 걸리거나 메모리 부족으로 실패할 수 있습니다."
        );

        new Thread(() -> {
            try {
                String result = nativeLoadModel(modelFile.getAbsolutePath());

                if (result == null || result.trim().isEmpty()) {
                    result = "nativeLoadModel returned empty result";
                }

                final String finalResult = result;
                lastModelLoadResult = finalResult;

                runOnUiThread(() -> {
                    statusText.setText(
                            "Whisper 모델 로드 확인 완료!\n\n" +
                            finalResult
                    );

                    Toast.makeText(this, "모델 로드 테스트 완료", Toast.LENGTH_LONG).show();
                    updateButtons();
                });

            } catch (Throwable e) {
                lastModelLoadResult =
                        e.getClass().getSimpleName() + "\n" +
                        String.valueOf(e.getMessage());

                runOnUiThread(() -> {
                    showError("Whisper 모델 로드 실패", e);
                    updateButtons();
                });
            }
        }).start();
    }

    private boolean ensureNativeLibraryLoaded() {
        if (nativeLibraryLoaded) {
            return true;
        }

        if (nativeLoadTried) {
            return false;
        }

        nativeLoadTried = true;

        try {
            System.loadLibrary("mobilestt");
            nativeLibraryLoaded = true;
            nativeLoadError = "";
            return true;
        } catch (Throwable e) {
            nativeLoadError =
                    e.getClass().getSimpleName() + "\n" +
                    String.valueOf(e.getMessage());

            nativeLibraryLoaded = false;
            return false;
        }
    }

    private File getModelDirectory() {
        File base = getExternalFilesDir(null);
        if (base == null) {
            base = getFilesDir();
        }

        return new File(base, "models");
    }

    private void copyUriToFile(
            Uri sourceUri,
            File outputFile,
            String label,
            long expectedSize
    ) throws IOException {
        File parent = outputFile.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IOException("폴더 생성 실패: " + parent.getAbsolutePath());
        }

        InputStream inputStream = getContentResolver().openInputStream(sourceUri);
        if (inputStream == null) {
            throw new IOException(label + " 입력 스트림을 열 수 없습니다.");
        }

        try (
                InputStream input = inputStream;
                OutputStream output = new FileOutputStream(outputFile, false)
        ) {
            byte[] buffer = new byte[1024 * 1024];
            long copied = 0L;
            long lastUiUpdate = 0L;

            int read;
            while ((read = input.read(buffer)) != -1) {
                output.write(buffer, 0, read);
                copied += read;

                long now = System.currentTimeMillis();
                if (now - lastUiUpdate > 500L) {
                    lastUiUpdate = now;
                    final long copiedNow = copied;

                    runOnUiThread(() -> {
                        statusText.setText(
                                label + " 복사 중...\n\n" +
                                "복사됨: " + formatBytes(copiedNow) + "\n" +
                                "전체: " + formatBytes(expectedSize) + "\n" +
                                progressText(copiedNow, expectedSize)
                        );
                    });
                }
            }

            output.flush();
        }

        if (!outputFile.exists() || outputFile.length() <= 0) {
            throw new IOException(label + " 복사 결과 파일이 비어 있습니다.");
        }
    }

    private String progressText(long copied, long total) {
        if (total <= 0) {
            return "";
        }

        int percent = (int) ((copied * 100L) / total);
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;

        return "진행률: " + percent + "%";
    }

    private void writeTestTxt(Uri outputUri) {
        String text = buildTestText();

        try {
            OutputStream outputStream = getContentResolver().openOutputStream(outputUri);

            if (outputStream == null) {
                throw new IOException("출력 스트림을 열 수 없습니다.");
            }

            try (OutputStreamWriter writer = new OutputStreamWriter(
                    outputStream,
                    StandardCharsets.UTF_8
            )) {
                writer.write(text);
                writer.flush();
            }

            statusText.setText(
                    "TXT 파일 저장 성공!\n\n" +
                    "저장된 파일 URI:\n" +
                    outputUri + "\n\n" +
                    "5단계 테스트가 완료되었습니다."
            );

            Toast.makeText(this, "TXT 저장 완료", Toast.LENGTH_LONG).show();

        } catch (Throwable e) {
            showError("TXT 파일 저장 실패", e);
        }
    }

    private String buildTestText() {
        StringBuilder sb = new StringBuilder();

        sb.append("Mobile STT 5단계 테스트 결과\n");
        sb.append("============================\n\n");

        sb.append("현재 단계: 5단계\n");
        sb.append("기능: WAV 선택 + Whisper 모델 선택 + 내부 복사 + 네이티브 연결 + 모델 로드 확인\n\n");

        sb.append("생성 시간: ");
        sb.append(new SimpleDateFormat(
                "yyyy-MM-dd HH:mm:ss",
                Locale.KOREA
        ).format(new Date()));
        sb.append("\n\n");

        sb.append("[선택된 오디오]\n");
        sb.append("이름: ").append(selectedAudioName).append("\n");
        sb.append("크기: ").append(formatBytes(selectedAudioSize)).append("\n");
        sb.append("URI: ").append(selectedAudioUri).append("\n\n");

        sb.append("[선택된 Whisper 모델]\n");
        sb.append("이름: ").append(selectedModelName).append("\n");
        sb.append("크기: ").append(formatBytes(selectedModelSize)).append("\n");
        sb.append("URI: ").append(selectedModelUri).append("\n\n");

        sb.append("[복사된 오디오 파일]\n");
        if (copiedAudioFile != null) {
            sb.append("경로: ").append(copiedAudioFile.getAbsolutePath()).append("\n");
            sb.append("크기: ").append(formatBytes(copiedAudioFile.length())).append("\n");
        } else {
            sb.append("없음\n");
        }
        sb.append("\n");

        sb.append("[복사된 Whisper 모델 파일]\n");
        if (copiedModelFile != null) {
            sb.append("경로: ").append(copiedModelFile.getAbsolutePath()).append("\n");
            sb.append("크기: ").append(formatBytes(copiedModelFile.length())).append("\n");
        } else {
            sb.append("없음\n");
        }
        sb.append("\n");

        sb.append("[네이티브 연결 확인 결과]\n");
        sb.append(lastNativeCheckResult).append("\n\n");

        sb.append("[Whisper 모델 로드 확인 결과]\n");
        sb.append(lastModelLoadResult).append("\n\n");

        sb.append("다음 단계에서는 짧은 WAV 파일을 실제로 STT 변환합니다.\n");

        return sb.toString();
    }

    private String getDisplayName(Uri uri) {
        String result = "selected_file";

        try (Cursor cursor = getContentResolver().query(
                uri,
                new String[]{OpenableColumns.DISPLAY_NAME},
                null,
                null,
                null
        )) {
            if (cursor != null && cursor.moveToFirst()) {
                int nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);

                if (nameIndex >= 0) {
                    String name = cursor.getString(nameIndex);
                    if (name != null && !name.trim().isEmpty()) {
                        return name;
                    }
                }
            }
        } catch (Throwable ignored) {
        }

        String uriText = uri.toString();
        int slash = uriText.lastIndexOf('/');
        if (slash >= 0 && slash + 1 < uriText.length()) {
            result = uriText.substring(slash + 1);
        }

        return result;
    }

    private long getFileSize(Uri uri) {
        try (Cursor cursor = getContentResolver().query(
                uri,
                new String[]{OpenableColumns.SIZE},
                null,
                null,
                null
        )) {
            if (cursor != null && cursor.moveToFirst()) {
                int sizeIndex = cursor.getColumnIndex(OpenableColumns.SIZE);

                if (sizeIndex >= 0 && !cursor.isNull(sizeIndex)) {
                    return cursor.getLong(sizeIndex);
                }
            }
        } catch (Throwable ignored) {
        }

        return -1L;
    }

    private String sanitizeFileName(String name, String fallback) {
        if (name == null || name.trim().isEmpty()) {
            name = fallback;
        }

        String cleaned = name
                .replaceAll("[\\\\/:*?\"<>|]", "_")
                .replace('\n', '_')
                .replace('\r', '_')
                .trim();

        if (cleaned.isEmpty()) {
            cleaned = fallback;
        }

        if (cleaned.length() > 80) {
            cleaned = cleaned.substring(0, 80);
        }

        return cleaned;
    }

    private String formatBytes(long bytes) {
        if (bytes < 0) {
            return "알 수 없음";
        }

        if (bytes < 1024) {
            return bytes + " B";
        }

        double kb = bytes / 1024.0;
        if (kb < 1024) {
            return String.format(Locale.US, "%.1f KB", kb);
        }

        double mb = kb / 1024.0;
        if (mb < 1024) {
            return String.format(Locale.US, "%.1f MB", mb);
        }

        double gb = mb / 1024.0;
        return String.format(Locale.US, "%.1f GB", gb);
    }

    private String nowForFileName() {
        return new SimpleDateFormat(
                "yyyyMMdd_HHmmss",
                Locale.US
        ).format(new Date());
    }

    private void updateButtons() {
        boolean hasAudio = selectedAudioUri != null;
        boolean hasModel = selectedModelUri != null;
        boolean copied = copiedAudioFile != null && copiedModelFile != null;

        if (copyButton != null) {
            copyButton.setEnabled(hasAudio && hasModel);
        }

        if (nativeCheckButton != null) {
            nativeCheckButton.setEnabled(true);
        }

        if (modelLoadButton != null) {
            modelLoadButton.setEnabled(copied);
        }

        if (saveTxtButton != null) {
            saveTxtButton.setEnabled(copied);
        }

        if (pickAudioButton != null) {
            pickAudioButton.setEnabled(true);
        }

        if (pickModelButton != null) {
            pickModelButton.setEnabled(true);
        }
    }

    private void setButtonsEnabled(boolean enabled) {
        if (pickAudioButton != null) {
            pickAudioButton.setEnabled(enabled);
        }

        if (pickModelButton != null) {
            pickModelButton.setEnabled(enabled);
        }

        if (copyButton != null) {
            copyButton.setEnabled(enabled);
        }

        if (nativeCheckButton != null) {
            nativeCheckButton.setEnabled(enabled);
        }

        if (modelLoadButton != null) {
            modelLoadButton.setEnabled(enabled);
        }

        if (saveTxtButton != null) {
            saveTxtButton.setEnabled(enabled);
        }
    }

    private void showError(String title, Throwable e) {
        String message =
                title + "\n\n" +
                e.getClass().getSimpleName() + "\n" +
                String.valueOf(e.getMessage());

        if (statusText != null) {
            statusText.setText(message);
        }

        Toast.makeText(this, title, Toast.LENGTH_LONG).show();
    }

    private void showFatalError(Throwable e) {
        TextView textView = new TextView(this);
        textView.setText(
                "앱 화면 생성 중 오류가 발생했습니다.\n\n" +
                e.getClass().getSimpleName() + "\n" +
                String.valueOf(e.getMessage())
        );
        textView.setTextSize(16f);
        textView.setPadding(40, 40, 40, 40);
        setContentView(textView);
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density);
    }
}
