"use client";
import RedirectOnAuth from "@/app/login/auth/redirectOnAuth";
import Dash from "@/app/dash/dash";

export default function DashLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <section className="h-screen bg-[#212936]">
      <RedirectOnAuth page="dash" />
      {Dash(children)}
    </section>
  );
}
