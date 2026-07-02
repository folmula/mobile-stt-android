package com.example.mobilestt;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.pdf.PdfDocument;

import java.io.IOException;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

public final class PdfWriterUtil {

    private PdfWriterUtil() {
    }

    public static void writePdf(
            OutputStream outputStream,
            String title,
            String text
    ) throws IOException {
        PdfDocument pdf = new PdfDocument();

        final int pageWidth = 595;
        final int pageHeight = 842;
        final float margin = 36f;
        final float maxWidth = pageWidth - margin * 2f;

        Paint titlePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        titlePaint.setTextSize(16f);
        titlePaint.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));

        Paint bodyPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        bodyPaint.setTextSize(10.5f);
        bodyPaint.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL));

        float lineHeight = 15f;

        int pageNo = 1;
        PdfDocument.Page page = pdf.startPage(
                new PdfDocument.PageInfo.Builder(pageWidth, pageHeight, pageNo).create()
        );

        Canvas canvas = page.getCanvas();
        float y = margin;

        canvas.drawText(title, margin, y, titlePaint);
        y += 28f;

        String[] paragraphs = text.split("\n", -1);

        for (String paragraph : paragraphs) {
            List<String> lines = wrapLine(paragraph, bodyPaint, maxWidth);

            for (String line : lines) {
                if (y > pageHeight - margin) {
                    pdf.finishPage(page);

                    pageNo++;
                    page = pdf.startPage(
                            new PdfDocument.PageInfo.Builder(pageWidth, pageHeight, pageNo).create()
                    );

                    canvas = page.getCanvas();
                    y = margin;
                }

                canvas.drawText(line, margin, y, bodyPaint);
                y += lineHeight;
            }
        }

        pdf.finishPage(page);
        pdf.writeTo(outputStream);
        pdf.close();
    }

    private static List<String> wrapLine(
            String paragraph,
            Paint paint,
            float maxWidth
    ) {
        List<String> result = new ArrayList<>();

        if (paragraph == null || paragraph.length() == 0) {
            result.add("");
            return result;
        }

        StringBuilder line = new StringBuilder();

        for (int i = 0; i < paragraph.length(); i++) {
            char ch = paragraph.charAt(i);

            if (ch == '\r') {
                continue;
            }

            String candidate = line.toString() + ch;

            if (paint.measureText(candidate) <= maxWidth || line.length() == 0) {
                line.append(ch);
            } else {
                result.add(line.toString());
                line.setLength(0);
                line.append(ch);
            }
        }

        if (line.length() > 0) {
            result.add(line.toString());
        }

        return result;
    }
}

