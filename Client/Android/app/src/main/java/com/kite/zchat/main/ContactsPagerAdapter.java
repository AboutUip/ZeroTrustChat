package com.kite.zchat.main;

import android.os.Bundle;

import androidx.annotation.NonNull;
import androidx.fragment.app.Fragment;
import androidx.viewpager2.adapter.FragmentStateAdapter;

/** 通讯录：好友 / 群聊 子页。 */
public final class ContactsPagerAdapter extends FragmentStateAdapter {

    private final Bundle args;

    public ContactsPagerAdapter(@NonNull ContactsFragment parent) {
        super(parent);
        Bundle b = parent.getArguments();
        this.args = b != null ? new Bundle(b) : new Bundle();
    }

    @NonNull
    @Override
    public Fragment createFragment(int position) {
        Fragment f = position == 0 ? new FriendsContactsFragment() : new GroupsContactsFragment();
        f.setArguments(new Bundle(args));
        return f;
    }

    @Override
    public int getItemCount() {
        return 2;
    }
}
