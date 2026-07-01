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

import java.io.IOException;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class MainActivity extends Activity {

    private static final int REQ_PICK_AUDIO = 1001;
    private static final int REQ_CREATE_TXT = 1002;

    private TextView statusText;
    private TextView audioText;
    private Button saveTxtButton;

    private Uri selectedAudioUri;
    private String selectedAudioName = "";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

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
                "2단계 테스트입니다.\n\n" +
                "1. WAV 파일을 선택합니다.\n" +
                "2. TXT 파일 저장을 테스트합니다.\n\n" +
                "아직 STT 기능은 없습니다."
        );
        statusText.setTextSize(16f);
        statusText.setPadding(0, dp(20), 0, dp(16));

        audioText = new TextView(this);
        audioText.setText("선택된 오디오 파일: 없음");
        audioText.setTextSize(15f);
        audioText.setPadding(0, dp(8), 0, dp(16));

        Button pickAudioButton = new Button(this);
        pickAudioButton.setText("WAV 오디오 파일 선택");
        pickAudioButton.setAllCaps(false);
        pickAudioButton.setOnClickListener(v -> openAudioPicker());

        saveTxtButton = new Button(this);
        saveTxtButton.setText("테스트 TXT 파일 저장");
        saveTxtButton.setAllCaps(false);
        saveTxtButton.setEnabled(false);
        saveTxtButton.setOnClickListener(v -> openTxtCreator());

        root.addView(
                title,
                new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                )
        );

        root.addView(
                statusText,
                new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                )
        );

        root.addView(
                audioText,
                new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                )
        );

        root.addView(
                pickAudioButton,
                new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                )
        );

        root.addView(
                saveTxtButton,
                new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                )
        );

        scrollView.addView(root);
        setContentView(scrollView);
    }

    private void openAudioPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("audio/*");

        String[] mimeTypes = new String[]{
                "audio/wav",
                "audio/x-wav",
                "audio/wave",
                "audio/*"
        };

        intent.putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);

        startActivityForResult(intent, REQ_PICK_AUDIO);
    }

    private void openTxtCreator() {
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("text/plain");

        String fileName = "mobile_stt_test_" + nowForFileName() + ".txt";
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

        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            return;
        }

        Uri uri = data.getData();

        if (requestCode == REQ_PICK_AUDIO) {
            selectedAudioUri = uri;
            selectedAudioName = getDisplayName(uri);

            try {
                int flags = data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION;
                if (flags != 0) {
                    getContentResolver().takePersistableUriPermission(uri, flags);
                }
            } catch (Exception ignored) {
                // 일부 기기에서는 영구 권한 저장이 실패할 수 있지만,
                // 현재 선택한 파일을 바로 쓰는 데는 문제가 없는 경우가 많습니다.
            }

            audioText.setText(
                    "선택된 오디오 파일:\n" +
                    selectedAudioName + "\n\n" +
                    "URI:\n" +
                    selectedAudioUri
            );

            statusText.setText(
                    "WAV 파일 선택이 완료되었습니다.\n\n" +
                    "이제 '테스트 TXT 파일 저장' 버튼을 눌러 저장공간에 TXT 파일을 만들어보세요."
            );

            saveTxtButton.setEnabled(true);
        }

        if (requestCode == REQ_CREATE_TXT) {
            writeTestTxt(uri);
        }
    }

    private void writeTestTxt(Uri outputUri) {
        String text = buildTestText();

        try {
            OutputStream outputStream = getContentResolver().openOutputStream(outputUri, "w");

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
                    "다음 단계에서 실제 STT 결과를 이 방식으로 저장하게 됩니다."
            );

            Toast.makeText(this, "TXT 저장 완료", Toast.LENGTH_LONG).show();

        } catch (Exception e) {
            statusText.setText(
                    "TXT 파일 저장 실패\n\n" +
                    e.getClass().getSimpleName() + "\n" +
                    e.getMessage()
            );

            Toast.makeText(this, "TXT 저장 실패", Toast.LENGTH_LONG).show();
        }
    }

    private String buildTestText() {
        StringBuilder sb = new StringBuilder();

        sb.append("Mobile STT 테스트 결과\n");
        sb.append("====================\n\n");

        sb.append("현재 단계: 2단계\n");
        sb.append("기능: WAV 파일 선택 + TXT 저장 테스트\n\n");

        sb.append("생성 시간: ");
        sb.append(new SimpleDateFormat(
                "yyyy-MM-dd HH:mm:ss",
                Locale.KOREA
        ).format(new Date()));
        sb.append("\n\n");

        sb.append("선택된 오디오 파일 이름:\n");
        sb.append(selectedAudioName.isEmpty() ? "없음" : selectedAudioName);
        sb.append("\n\n");

        sb.append("선택된 오디오 URI:\n");
        sb.append(selectedAudioUri == null ? "없음" : selectedAudioUri.toString());
        sb.append("\n\n");

        sb.append("다음 단계에서는 여기에 STT 변환 결과가 저장됩니다.\n");

        return sb.toString();
    }

    private String getDisplayName(Uri uri) {
        String result = uri.toString();

        try (Cursor cursor = getContentResolver().query(
                uri,
                new String[]{
                        OpenableColumns.DISPLAY_NAME,
                        OpenableColumns.SIZE
                },
                null,
                null,
                null
        )) {
            if (cursor != null && cursor.moveToFirst()) {
                int nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                int sizeIndex = cursor.getColumnIndex(OpenableColumns.SIZE);

                String name = "";
                long size = -1L;

                if (nameIndex >= 0) {
                    name = cursor.getString(nameIndex);
                }

                if (sizeIndex >= 0 && !cursor.isNull(sizeIndex)) {
                    size = cursor.getLong(sizeIndex);
                }

                if (name != null && !name.isEmpty()) {
                    if (size >= 0) {
                        return name + " (" + formatBytes(size) + ")";
                    } else {
                        return name;
                    }
                }
            }
        } catch (Exception ignored) {
            // 파일 이름을 못 읽으면 URI 문자열을 대신 사용합니다.
        }

        return result;
    }

    private String formatBytes(long bytes) {
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

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density);
    }
}

