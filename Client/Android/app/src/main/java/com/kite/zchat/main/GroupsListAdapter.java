package com.kite.zchat.main;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.kite.zchat.R;

import java.util.ArrayList;
import java.util.List;

public final class GroupsListAdapter extends RecyclerView.Adapter<GroupsListAdapter.Holder> {

    public static final class ContactGroupItem {
        public final String groupIdHex;
        public final String name;
        public final int memberCount;

        public ContactGroupItem(String groupIdHex, String name, int memberCount) {
            this.groupIdHex = groupIdHex != null ? groupIdHex : "";
            this.name = name != null ? name : "";
            this.memberCount = memberCount;
        }
    }

    private final List<ContactGroupItem> items = new ArrayList<>();

    public void setItems(List<ContactGroupItem> next) {
        items.clear();
        if (next != null) {
            items.addAll(next);
        }
        notifyDataSetChanged();
    }

    @NonNull
    @Override
    public Holder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View v =
                LayoutInflater.from(parent.getContext())
                        .inflate(R.layout.item_contact_group, parent, false);
        return new Holder(v);
    }

    @Override
    public void onBindViewHolder(@NonNull Holder h, int position) {
        ContactGroupItem it = items.get(position);
        String title = it.name.isEmpty() ? it.groupIdHex : it.name;
        h.name.setText(title);
        h.members.setText(
                h.itemView
                        .getContext()
                        .getString(R.string.contacts_members_format, it.memberCount));
    }

    @Override
    public int getItemCount() {
        return items.size();
    }

    static final class Holder extends RecyclerView.ViewHolder {
        final TextView name;
        final TextView members;

        Holder(@NonNull View itemView) {
            super(itemView);
            name = itemView.findViewById(R.id.contactGroupName);
            members = itemView.findViewById(R.id.contactGroupMembers);
        }
    }
}
