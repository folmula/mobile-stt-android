package com.example.mobilestt;

import android.app.Activity;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

public class MainActivity extends Activity {

    private TextView message;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(24), dp(24), dp(24));

        TextView title = new TextView(this);
        title.setText("Mobile STT");
        title.setTextSize(26f);
        title.setTypeface(Typeface.DEFAULT_BOLD);

        message = new TextView(this);
        message.setText(
                "1단계 APK 빌드 성공 확인용 앱입니다.\n\n" +
                "아직 STT 기능은 없습니다.\n" +
                "다음 단계에서 파일 선택 기능을 추가합니다."
        );
        message.setTextSize(16f);
        message.setPadding(0, dp(20), 0, dp(20));

        Button button = new Button(this);
        button.setText("앱 실행 확인");
        button.setAllCaps(false);

        button.setOnClickListener(v -> {
            message.setText(
                    "앱이 정상 실행되었습니다.\n\n" +
                    "다음 단계:\n" +
                    "1. 휴대폰 저장공간에서 WAV 파일 선택\n" +
                    "2. TXT 파일 저장 기능 추가\n" +
                    "3. whisper.cpp STT 연결"
            );
        });

        root.addView(
                title,
                new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                )
        );

        root.addView(
                message,
                new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                )
        );

        root.addView(
                button,
                new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                )
        );

        setContentView(root);
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density);
    }
}

