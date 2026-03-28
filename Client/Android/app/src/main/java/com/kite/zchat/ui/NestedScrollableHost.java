package com.kite.zchat.ui;

import android.content.Context;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.ViewParent;
import android.widget.FrameLayout;

import androidx.annotation.Nullable;
import androidx.viewpager2.widget.ViewPager2;

/**
 * 包裹内层 {@link ViewPager2}，解决「外层横向 ViewPager2 ↔ 内层横向 ViewPager2」手势抢占：
 * 内层仍可滑动时由子 ViewPager 处理；滑到边缘时放行给外层，便于连续切换主 Tab。
 * 思路对齐 Android ViewPager2 官方示例中的 NestedScrollableHost。
 */
public final class NestedScrollableHost extends FrameLayout {

    private final int touchSlop;
    private float initialX;
    private float initialY;

    public NestedScrollableHost(Context context) {
        this(context, null);
    }

    public NestedScrollableHost(Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        touchSlop = ViewConfiguration.get(context).getScaledTouchSlop();
    }

    @Nullable
    private ViewPager2 findParentViewPager() {
        View v = this;
        while (true) {
            ViewParent p = v.getParent();
            if (p == null) {
                return null;
            }
            if (p instanceof ViewPager2) {
                return (ViewPager2) p;
            }
            if (p instanceof View) {
                v = (View) p;
            } else {
                return null;
            }
        }
    }

    @Nullable
    private ViewPager2 getChildViewPager() {
        if (getChildCount() == 0) {
            return null;
        }
        View c = getChildAt(0);
        return c instanceof ViewPager2 ? (ViewPager2) c : null;
    }

    private void handleTouch(MotionEvent ev) {
        ViewPager2 parentVp = findParentViewPager();
        ViewPager2 childVp = getChildViewPager();
        if (parentVp == null || childVp == null) {
            return;
        }
        if (parentVp.getOrientation() != ViewPager2.ORIENTATION_HORIZONTAL
                || childVp.getOrientation() != ViewPager2.ORIENTATION_HORIZONTAL) {
            return;
        }

        int action = ev.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            initialX = ev.getX();
            initialY = ev.getY();
            getParent().requestDisallowInterceptTouchEvent(true);
        } else if (action == MotionEvent.ACTION_MOVE) {
            float dx = ev.getX() - initialX;
            float dy = ev.getY() - initialY;
            if (Math.abs(dx) > touchSlop && Math.abs(dx) > Math.abs(dy)) {
                boolean canScroll = childVp.canScrollHorizontally(dx > 0 ? -1 : 1);
                getParent().requestDisallowInterceptTouchEvent(canScroll);
            }
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            getParent().requestDisallowInterceptTouchEvent(false);
        }
    }

    @Override
    public boolean onInterceptTouchEvent(MotionEvent ev) {
        handleTouch(ev);
        return super.onInterceptTouchEvent(ev);
    }

    @Override
    public boolean onTouchEvent(MotionEvent ev) {
        handleTouch(ev);
        return super.onTouchEvent(ev);
    }
}
