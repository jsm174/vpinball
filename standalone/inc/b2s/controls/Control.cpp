#include "stdafx.h"

#include "Control.h"

Control::Control()
{  
   m_pParent = NULL;
   m_szName = "";
   m_rect = { 0, 0, 0, 0 };
   m_visible = true;
   m_backColor = RGB(0, 0, 0);
   m_handleCreated = false;
   m_pBackgroundImage = NULL;
}

Control::~Control()
{
}

void Control::OnPaint(B2S::Graphics* pGraphics)
{
   if (m_visible) {
      for (Control* child : m_children) {
         if (child->IsVisible())
            child->OnPaint(pGraphics);
      }
   }
}

void Control::OnHandleCreated()
{
}

void Control::AddControl(Control* control)
{
   m_children.push_back(control);
   control->m_pParent = this;
   control->m_handleCreated = true;
   control->OnHandleCreated();
}

Control* Control::GetControl(const string& szName)
{
   for (Control* child : m_children) {
      if (child->GetName() == szName)
         return child;
   }
   return NULL;
}

void Control::BringToFront()
{
   if (m_pParent) {
      auto it = std::find(m_pParent->m_children.begin(), m_pParent->m_children.end(), this);
      if (it != m_pParent->m_children.end()) {
         m_pParent->m_children.erase(it);
         m_pParent->m_children.insert(m_pParent->m_children.begin(), this);
      }
   }
}

void Control::Refresh()
{
}

void Control::Invalidate()
{
}