package com.example.mobilestt;

import android.app.Activity;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.TextView;

public class SafeActivity extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        TextView textView = new TextView(this);
        textView.setText(
                "Mobile STT 실행 복구 성공\n\n" +
                "앱이 정상적으로 실행되었습니다.\n\n" +
                "다음 단계에서 파일 선택 기능을 다시 천천히 추가하겠습니다."
        );
        textView.setTextSize(20f);
        textView.setGravity(Gravity.CENTER);
        textView.setPadding(40, 40, 40, 40);

        setContentView(textView);
    }
}

