package com.example.mobilestt

import android.app.Activity
import android.graphics.Typeface
import android.os.Bundle
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

class MainActivity : Activity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(24), dp(24), dp(24), dp(24))
        }

        val title = TextView(this).apply {
            text = "Mobile STT"
            textSize = 26f
            typeface = Typeface.DEFAULT_BOLD
        }

        val message = TextView(this).apply {
            text = """
                1단계 APK 빌드 성공 확인용 앱입니다.

                아직 STT 기능은 없습니다.
                다음 단계에서 파일 선택 기능을 추가합니다.
            """.trimIndent()

            textSize = 16f
            setPadding(0, dp(20), 0, dp(20))
        }

        val button = Button(this).apply {
            text = "앱 실행 확인"
            isAllCaps = false

            setOnClickListener {
                message.text = """
                    앱이 정상 실행되었습니다.

                    다음 단계:
                    1. 휴대폰 저장공간에서 WAV 파일 선택
                    2. TXT 파일 저장 기능 추가
                    3. whisper.cpp STT 연결
                """.trimIndent()
            }
        }

        root.addView(
            title,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )
        )

        root.addView(
            message,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )
        )

        root.addView(
            button,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )
        )

        setContentView(root)
    }

    private fun dp(value: Int): Int {
        return (value * resources.displayMetrics.density).toInt()
    }
}
