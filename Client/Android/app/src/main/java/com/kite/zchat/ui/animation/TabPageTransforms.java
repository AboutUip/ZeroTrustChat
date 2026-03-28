package com.kite.zchat.ui.animation;

import androidx.viewpager2.widget.CompositePageTransformer;
import androidx.viewpager2.widget.MarginPageTransformer;
import androidx.viewpager2.widget.ViewPager2;

/**
 * ViewPager2 分页过渡：轻微边距 + 相邻页透明度与缩放，避免生硬切屏。
 */
public final class TabPageTransforms {

    private TabPageTransforms() {}

    public static void apply(ViewPager2 pager, float pageMarginDp) {
        float density = pager.getResources().getDisplayMetrics().density;
        int marginPx = Math.round(pageMarginDp * density);

        CompositePageTransformer composite = new CompositePageTransformer();
        composite.addTransformer(new MarginPageTransformer(marginPx));
        composite.addTransformer(
                (page, position) -> {
                    float abs = Math.abs(position);
                    float scale = 1f - 0.06f * abs;
                    page.setScaleX(scale);
                    page.setScaleY(scale);
                    page.setAlpha(1f - 0.22f * abs);
                });
        pager.setPageTransformer(composite);
    }
}
