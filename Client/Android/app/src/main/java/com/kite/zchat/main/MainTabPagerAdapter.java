package com.kite.zchat.main;

import androidx.annotation.NonNull;
import androidx.fragment.app.Fragment;
import androidx.fragment.app.FragmentActivity;
import androidx.viewpager2.adapter.FragmentStateAdapter;

public final class MainTabPagerAdapter extends FragmentStateAdapter {

    private final String profileHost;
    private final int profilePort;

    public MainTabPagerAdapter(
            @NonNull FragmentActivity activity, String profileHost, int profilePort) {
        super(activity);
        this.profileHost = profileHost;
        this.profilePort = profilePort;
    }

    @NonNull
    @Override
    public Fragment createFragment(int position) {
        if (position == 0) {
            return CommunicationFragment.newInstance(profileHost, profilePort);
        }
        if (position == 1) {
            return ContactsFragment.newInstance(profileHost, profilePort);
        }
        if (position == 2) {
            return FeaturesFragment.newInstance(profileHost, profilePort);
        }
        if (position == 3) {
            return ProfileFragment.newInstance(profileHost, profilePort);
        }
        throw new IllegalArgumentException("tab " + position);
    }

    @Override
    public int getItemCount() {
        return 4;
    }
}
